# 深入探究文件I/O

## [0x00]原子操作竞争条件

在探究系统调用时会反复涉及原子操作的概念。所有系统调用都是以原子操作方式执行的。之所以这么说，是指内核保证了某系统调用中的所有步骤会作为独立操作而一次性加以执行，其间不会为其他进程或线程所中断。

原子性是某些操作得以圆满成功的关键所在。特别是它规避了竞争状态（race conditions）（有时也称为竞争冒险）。竞争状态是这样一种情形：操作共享资源的两个进程（或线程），其结果取决于一个无法预期的顺序，即这些进程 获得CPU使用权的先后相对顺序。

接下来，将讨论涉及文件I/O的两种竞争状态，并展示了如何使用open()的标志位，来保证相关文件操作的原子性，从而消除这些竞争状态。

### 以独占方式创建一个文件

[FileIO]中曾述及：当同时指定O_EXCL与O_CREAT作为open()的标志位时，如果要打开的文件已然存在，则open()将返回一个错误。这提供了一种机制，保证进程是打开文件的创建者。对文件是否存在的检查和创建文件属于同一原子操作。要理解这一点的重要性，请思考下列程序所示代码，该段代码中并未使用O_EXCL标志。（在此，为了对执行该程序的不同进程加以区分，在输出信息中打印有通过调用getpid()所返回的进程号。）

```c
fd = open(argv[1], O_WRONLY);
if(fd != -1){
  printf("[PID %ld] File \"%s\" already exists\n", (long)getpid(), argv[1]);
  close(fd);
}else{
  if(errno != ENOENT){
    errExit("open");
  }else{
    /* WINDOW FOR FAILURE */
    fd = open(argv[1], O_WRONLY|O_CREAT, S_IRUSER|S_IWUSER);
    if(fd == -1){
      errExit("open");
    }
    printf("[PID %ld] Created file \"%s\" exclusively\n", (long)getpid(), argv[1]);
  }
}
```

上述程序中所示的代码，除了要啰啰嗦嗦地调用open()两次外，还潜伏着一个bug。假设如下情况：当第一次调用open()时，希望打开的文件还不存在，而当第二次调用open()时，其他进程已经创建了该文件。如图5-1所示，若内核调度器判断出分配给A进程的时间片已经耗尽，并将CPU使用权交给B进程，就可能会发生这种问题。再比如两个进程在一个多CPU系统上同时运行时，也会出现这种情况。下图展示了两个进程同时执行程序清单5-1中代码的情形。在这一场景下，进程A将得出错误的结论：目标文件是由自己创建的。因为无论目标文件存在与否，进程A对open()的第二次调用都会成功。

![](../../images/WechatIMG24.png)

虽然进程将自己误认为文件创建者的可能性相对较小，但毕竟是存在的，这已然将此段代码置于不可靠的境地。操作的结果将依赖于对两个进程的调度顺序，这一事实也就意味着出现了竞争状态。

为了说明这段代码的确存在问题，可以用一段代码替换上述程序的注释行“处理文件不存在的情况”，在检查文件是否存在与创建文件这两个动作之间人为制造一个长时间的等待。

```c
printf("[PID %ld] File \"%s\" doesn't exist yet \n", (long)getpid(), argv[1]);
if(argc > 1){
  sleep(5);
  printf("[PID %ld] Done sleeping\n",(long)getpid());
}
```

sleep()库函数可将当前执行的进程挂起指定的秒数。

如果同时运行程序清单5-1中程序的两个实例，两个进程都会声称自己以独占方式创建了文件。

![](../../images/WechatIMG29.png)

从上面输出的倒数第二行可以发现，shell提示符里夹杂了第一个实例的输出信息。

由于第一个进程在检查文件是否存在和创建文件之间发生中断，造成两个进程都声称自己是文件的创建者。结合O_CREAT和O_EXCL标志来一次性的调用open()可以防止这种情况，因为这确保了检查文件和创建文件的步骤属于一个单一的原子(即不可中断)操作。

### 向文件尾部追加数据

用以说明原子操作必要性的第二个例子是：多个进程同时向同一个文件(例如，全局日志文件)尾部添加数据。为了达到这个目的，也许可以考虑在每个写进程中使用如下代码。

```c
if(lseek(fd, 0, SEEK_END) == -1){
  errExit("lseek");
}

if(write(fd, buf, len) != len){
  fatal("Partial/failed write");
}
```

但是，这段代码存在的缺陷与前一个例子如出一辙。如果第一个进程执行lseek()和write()之间，被执行相同的代码的第二个进程锁中断，那么这两个进程会在写入数据前，将文件偏移设置为相同位置，而当第一个进程再次获得调度时，会覆盖第二个进程已写入的数据。此时再次出现了竞争状态，因为执行的结果依赖于内核对两个进程的调度顺序。

要规避这一问题，需要将文件偏移量的移动与数据写操作纳入同一原子操作。在打开文件时加入O_APPEND标志就可以保证这一点。

有些文件系统（例如NFS）不支持 O_APPEND 标志。在这种情况下，内核会选择按如上代码所示的方式，施之以非原子操作的调用序列，从而可能导致上述的文件脏写入问题。

## [0x01]文件控制操作fcntl()

fcntl()系统调用对一个打开的文件描述符执行一系列控制操作。

```c
#include <unistd.h>
#include <fcntl.h>

int fcntl(int fd, int cmd, ... /* arg */ );
				Returns on success depends on cmd, or -1 on error.
```

cmd参数所支持的操作范围很广。本章随后各节会对其中的部分操作加以研讨，剩下的操作将在后续各章中进行论述。

fcntl()的第三个参数以省略号来表示，这意味着可以将其设置为不同的类型，或者加以省略。内核会依据cmd参数（如果有的话）的值来确定该参数的数据类型。

## [0x02]打开文件的状态标志

fcntl()的用途之一是针对一个打开的文件，获取或修改其访问模式和状态标志（这些值是通过指定open()调用的flag参数来设置的）。要获取这些设置，应将fcntl()的cmd参数设置为F_GETFL。

```c
int flags, accessMode;

flags = fcntl(fd, F_GETFL);
if(flags == -1){
  errExit("fcntl");
}
```

在上述代码之后，可以以如下代码测试文件是否以同步写方式打开：

```c
if(flags & O_SYNC){
  printf("write are synchronized\n");
}
```

SUSv3规定：针对一个打开的文件，只有通过open()或后续fcntl()的F_SETFL操作，才能对该文件的状态标志进行设置。然而在如下方面，Linux实现与标准有所偏离：如果一个程序编译时采用了5.10节所提及的打开大文件技术，那么当使用F_GETFL命令获取文件状态标志时，标志中将总是包含O_LARGEFILE标志。

判定文件的访问模式有一点复杂，这是因为O_RDONLY(0)、O_WRONLY(1)和O_RDWR(2)这3个常量并不与打开文件状态标志中的单个比特位对应。因此，要判定访问模式，需使用掩码O_ACCMODE与flag相与，将结果与3个常量进行比对，示例代码如下：

```c
accessMode = flags & O_ACCMODE;
if(accessMode == O_WRONLY || accessMode == O_RDWR){
  printf("file is writable\n");
}
```

可以使用fcntl()的F_SETFL命令来修改打开文件的某些状态标志。允许更改的标志有O_APPEND、O_NONBLOCK、O_NOATIME、O_ASYNC和O_DIRECT。系统将忽略对其他标志的修改操作。（有些其他的UNIX实现允许fcntl()修改其他标志，如O_SYNC。）

使用fcntl()修改文件状态标志，尤其适用于如下场景。

- 文件不是由调用程序打开的，所以程序也无法使用open()调用来控制文件的状态标志（例如，文件是3个标准输入输出描述符中的一员，这些描述符在程序启动之前就被打开）。
- 文件描述符的获取是通过open()之外的系统调用。比如pipe()调用，该调用创建一个管道，并返回两个文件描述符分别对应管道的两端。再比如socket()调用，该调用创建一个套接字并返回指向该套接字的文件描述符。

为了修改打开文件的状态标志，可以使用fcntl()的F_GETFL命令来获取当前标志的副本，然后修改需要变更的比特位，最后再次调用fcntl()函数的F_SETFL命令来更新此状态标志。因此，为了添加O_APPEND标志，可以编写如下代码：

```c
int flags;

flags = fcntl(fd, F_GETFL);
if(flags == -1){
  errExit("fcntl");
}
flags |= O_APPEND;
if(fcntl(fd, f_SETFL, flags) == -1){
  errExit("fcntl");
}
```

## [0x03]文件描述符和打开文件之间的关系

到目前为止，文件描述符和打开的文件之间似乎呈现出一一对应的关系。然而，实际并非如此。多个文件描述符指向同一打开文件，这既有可能，也属必要。这些文件描述符可在相同或不同的进程中打开。

要理解具体情况如何，需要查看由内核维护的3个数据结构。

- 进程级的文件描述符表。
- 系统级的打开文件表。
- 文件系统的i-node表。

针对每个进程，内核为其维护打开文件的描述符（open file descriptor）表。该表的每一条目都记录了单个文件描述符的相关信息，如下所示。

- 控制文件描述符操作的一组标志。（目前，此类标志仅定义了一个，即close-on-exec标志，将在27.4节予以介绍。）
- 对打开文件句柄的引用。

内核对所有打开的文件维护有一个系统级的描述表格（open file description table）。有时，也称之为打开文件表（open file table），并将表中各条目称为打开文件句柄（open file handle） 。一个打开文件句柄存储了与一个打开文件相关的全部信息，如下所示。

- 当前文件偏移量（调用read()和write()时更新，或使用lseek()直接修改）。
- 打开文件时所使用的状态标志（即，open()的flags参数）。
- 文件访问模式（如调用open()时所设置的只读模式、只写模式或读写模式）。
- 与信号驱动I/O相关的设置。
- 对该文件i-node对象的引用。

每个文件系统都会为驻留其上的所有文件建立一个i-node表。第14章将详细讨论i-node结构和文件系统的总体结构。这里只是列出每个文件的i-node信息，具体如下。

- 文件类型（例如，常规文件、套接字或FIFO）和访问权限。
- 一个指针，指向该文件所持有的锁的列表。
- 文件的各种属性，包括文件大小以及与不同类型操作相关的时间戳。

此处将忽略i-node在磁盘和内存中的表示差异。磁盘上的i-node记录了文件的固有属性，诸如：文件类型、访问权限和时间戳。访问一个文件时，会在内存中为i-node创建一个副本，其中记录了引用该i-node的打开文件句柄数量以及该i-node所在设备的主、从设备号，还包括一些打开文件时与文件相关的临时属性，例如：文件锁。

图5-2展示了文件描述符、打开的文件句柄以及i-node之间的关系。在下图中，两个进程拥有诸多打开的文件描述符。

![](../../images/WechatIMG38.png)

在进程A中，文件描述符1和20都指向同一个打开的文件句柄（标号为23）。这可能是通过调用dup()、dup2()或fcntl()而形成的（参见5.5节）。

进程A的文件描述符2和进程B的文件描述符2都指向同一个打开的文件句柄（标号为73）。这种情形可能在调用fork()后出现（即，进程A与进程B之间是父子关系），或者当某进程通过UNIX域套接字将一个打开的文件描述符传递给另一进程时，也会发生。

此外，进程A的描述符0和进程B的描述符3分别指向不同的打开文件句柄，但这些句柄均指向i-node表中的相同条目（1976），换言之，指向同一文件。发生这种情况是因为每个进程各自对同一文件发起了open()调用。同一个进程两次打开同一文件，也会发生类似情况。

上述讨论揭示出如下要点。

- 两个不同的文件描述符，若指向同一打开文件句柄，将共享同一文件偏移量。因此，如果通过其中一个文件描述符来修改文件偏移量（由调用read()、write()或lseek()所致），那么从另一文件描述符中也会观察到这一变化。无论这两个文件描述符分属于不同进程，还是同属于一个进程，情况都是如此。
- 要获取和修改打开的文件标志（例如，O_APPEND、O_NONBLOCK和O_ASYNC），可执行fcntl()的F_GETFL和F_SETFL操作，其对作用域的约束与上一条颇为类似。
- 相形之下，文件描述符标志（亦即，close-on-exec标志）为进程和文件描述符所私有。对这一标志的修改将不会影响同一进程或不同进程中的其他文件描述符。

## [0x04]复制文件描述符

Bourne shell的I/O重定向语法2>&1，意在通知shell把标准错误（文件描述符2）重定向到标准输出（文件描述符1）。因此，下列命令将把（因为shell按从左至右的顺序处理I/O重定向语句）标准输出和标准错误写入result.log文件：

`$ ./myscript > results.log 2>&1`

shell通过复制文件描述符2 实现了标准错误的重定向操作，因此文件描述符2与文件描述符1指向同一个打开文件句柄（类似于图5-2中进程A的描述符1和20指向同一打开文件句柄的情况）。可以通过调用dup()和dup2()来实现此功能。

请注意，要满足shell的这一要求，仅仅简单地打开results.log文件两次是远远不够的（第一次在描述符1上打开，第二次在描述符2上打开）。首先两个文件描述符不能共享相同的文件偏移量指针，因此有可能导致相互覆盖彼此的输出。再者打开的文件不一定就是磁盘文件。在如下命令中，标准错误就将和标准输出一起送达同一管道： 

`$ ./myscript 2>&1 | less`

dup()调用复制一个打开的文件描述符oldfd，并返回一个新描述符，二者都指向同一打开的文件句柄。系统会保证新描述符一定是编号值最低的未用文件描述符。

```c
#include <unistd.h>
int dup(int oldfd);
			Returns (new) file descriptor on success, or -1 on error.
```

假设发起如下调用：

`newfd = dup(1);`

再假定在正常情况下，shell已经代表程序打开了文件描述符0、1和2，且没有其他描述符在用，dup()调用会创建文件描述符1的副本，返回的文件描述符编号值为3。

如果希望返回文件描述符2，可以使用如下技术：

```c
close(2);
newfd = dup(1);
```

只有当描述符0已经打开时，这段代码方可工作。如果想进一步简化上述代码，同时总是能获得所期望的文件描述符，可以调用dup2()。

```c
#include <unistd.h>

int dup2(int oldfd, int newfd);
				Returns (new) file descriptor on success, or -1 on error.
```

dup2()系统调用会为oldfd参数所指定的文件描述符创建副本，其编号由newfd参数指定。如果由newfd参数所指定编号的文件描述符之前已经打开，那么dup2()会首先将其关闭。（dup2()调用会默然忽略newfd关闭期间出现的任何错误。故此，编码时更为安全的做法是：在调用dup2()之前，若newfd已经打开，则应显式调用close()将其关闭。）

前述调用close()和dup()的代码可以简化为：

`dup2(1, 2);`

若调用dup2()成功，则将返回副本的文件描述符编号（即newfd参数指定的值）。

如果oldfd并非有效的文件描述符，那么dup2()调用将失败并返回错误EBADF，且不关闭newfd。如果oldfd有效，且与newfd值相等，那么dup2()将什么也不做，不关闭newfd，并将其作为调用结果返回。

fcntl()的F_DUPFD操作是复制文件描述符的另一接口，更具灵活性。 

`newfd = fcntl(oldfd, F_DUPFD, startFd);`

该调用为oldfd创建一个副本，且将使用大于等于startfd的最小未用值作为描述符编号。该调用还能保证新描述符（newfd）编号落在特定的区间范围内。总是能将dup()和dup2()调用改写为对close()和fcntl()的调用，虽然前者更为简洁。（还需注意，正如手册页中所描述的，dup2()和fcntl()二者返回的errno错误码存在一些差别。）

由上图可知，文件描述符的正、副本之间共享同一打开文件句柄所含的文件偏移量和状态标志。然而，新文件描述符有其自己的一套文件描述符标志，且其close-on-exec标志（FD_CLOEXEC）总是处于关闭状态。下面将要介绍的接口，可以直接控制新文件描述符的close-on-exec标志。

dup3()系统调用完成的工作与dup2()相同，只是新增了一个附加参数flag，这是一个可以修改系统调用行为的位掩码。

```c
#define _GUN_SOURCE
#include <unistd.h>

int dup3(int oldfd, int newfd, int flags);
			Returns (new) file descriptor on success, or -1 on error.
```

目前，dup3()只支持一个标志O_CLOEXEC，这将促使内核为新文件描述符设置 close-on-exec标志（FD_CLOEXEC）。设计该标志的缘由，类似于4.3.1节对open()调用中O_CLOEXEC标志的描述。

dup3()系统调用始见于Linux 2.6.27，为Linux所特有。

Linux从2.6.24开始支持fcntl()用于复制文件描述符的附加命令：F_DUPFD_CLOEXEC。该标志不仅实现了与F_DUPFD相同的功能，还为新文件描述符设置close-on-exec标志。同样，此命令之所以得以一显身手，其原因也类似于open()调用中的O_CLOEXEC标志。SUSv3并未论及F_DUPFD_CLOEXEC标志，但SUSv4对其作了规范。

## [0x05]在文件特定偏移量处的I/O

系统调用pread()和pwrite()完成与read()和write()相类似的工作，只是前两者会在offset参数所指定的位置进行文件I/O操作，而非始于文件的当前偏移量处，且它们不会改变文件的当前偏移量。

```c
#include <unistd.h>

ssize_t pread(int fd, void *buf, size_t count, off_t offset);
					Returns number of bytes read. 0 on EOF, or -1 on error
ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);
				Returns number of bytes writen, or -1 on error.
```

pread()调用等同于将如下调用纳入同一原子操作：

```c
off_t orig;

orig = lseek(fd, 0, SEEK_CUR);
lseek(fd, offset, SEEK_SET);
s = read(fd, buf, len);
lseek(fd, orig, SEEK_SET);
```

对pread()和pwrite()而言，fd所指代的文件必须是可定位的（即允许对文件描述符执行lseek()调用）。

多线程应用为这些系统调用提供了用武之地。正如第29章所述，进程下辖的所有线程将共享同一文件描述符表。这也意味着每个已打开文件的文件偏移量为所有线程所共享。当调用pread()或pwrite()时，多个线程可同时对同一文件描述符执行I/O操作，且不会因其他线程修改文件偏移量而受到影响。如果还试图使用lseek()和read()(或write())来代替pread()（或pwrite()），那么将引发竞争状态，这类似于5.1节讨论O_APPEND标志时的描述（当多个进程的文件描述符指向相同的打开文件句柄时，使用pread()和 pwrite()系统调用同样能够避免进程间出现竞争状态）。

如果需要反复执行lseek()，并伴之以文件I/O，那么pread()和pwrite()系统调用在某些情况下是具有性能优势的。这是因为执行单个pread()（或pwrite()）系统调用的成本要低于执行lseek()和read()（或write()）两个系统调用。然而，较之于执行I/O实际所需的时间，系统调用的开销就有些相形见绌了。

## [0x06]分散输入和集中输出

和writev() readv()和writev()系统调用分别实现了分散输入和集中输出的功能。

```c
 #include <sys/uio.h>

ssize_t readv(int d, const struct iovec *iov, int iovcnt);
		Returns number of bytes read, 0 on EOF, or -1 on error.
ssize_t writev(int fildes, const struct iovec *iov, int iovcnt);
		Returns number of bytes writen, or -1 on error.
```

这些系统调用并非只对单个缓冲区进行读写操作，而是一次即可传输多个缓冲区的数据。数组iov定义了一组用来传输数据的缓冲区。整型数iovcnt则指定了iov的成员个数。iov中的每个成员都是如下形式的数据结构。

```
struct iovec {
	void *iov_base;
	size_t iov_len;
};
```

SUSv3标准允许系统实现对iov中的成员个数加以限制。系统实现可以通过定义<limits.h>文件中IOV_MAX来通告这一限额，程序也可以在系统运行时调用sysconf (_SC_IOV_MAX)来获取这一限额。（11.2节将介绍sysconf()。）SUSv3要求该限额不得少于16。Linux将IOV_MAX的值定义为1024，这是与内核对该向量大小的限制（由内核常量UIO_MAXIOV定义）相对应的。

然而，glibc对readv()和writev()的封装函数 还悄悄做了些额外工作。若系统调用因iovcnt参数值过大而失败，外壳函数将临时分配一块缓冲区，其大小足以容纳iov参数所有成员所描述的数据缓冲区，随后再执行read()或write()调用（参见后文对使用write()实现writev()功能的讨论）。

如下图所示的是一个关于iov、iovcnt以及iov指向缓冲区之间关系的示例。

![](../../images/WechatIMG52.png)

### 分散输入

readv()系统调用实现了分散输入的功能：从文件描述符fd所指代的文件中读取一片连续的字节，然后将其散置（“分散放置”）于iov指定的缓冲区中。这一散置动作从iov[0]开始，依次填满每个缓冲区。

原子性是readv()的重要属性。换言之，从调用进程的角度来看，当调用readv()时，内核在fd所指代的文件与用户内存之间一次性地完成了数据转移。这意味着，假设即使有另一进程（或线程）与其共享同一文件偏移量，且在调用readv()的同时企图修改文件偏移量，readv()所读取的数据仍将是连续的。

调用readv()成功将返回读取的字节数，若文件结束 将返回0。调用者必须对返回值进行检查，以验证读取的字节数是否满足要求。若数据不足以填充所有缓冲区，则只会占用 部分缓冲区，其中最后一个缓冲区可能只存有部分数据。

下面程序展示了readv()的用法。

```c
#include <sys/stat.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    int fd;
    struct iovec iov[3];
    struct stat myStruct;
    int x;
#define STR_SIZE 100
    char str[STR_SIZE];
    size_t numRead, totRequired;

    if(argc != 2 || strcmp(argv[1], "--help") == 0){
        printf("%s file \n", argv[0]);
    }

    fd = open(argv[1], O_RDONLY);
    if(fd == -1){
        perror("open");
        exit(1);
    }

    totRequired = 0;

    iov[0].iov_base = &myStruct;
    iov[0].iov_len = sizeof(struct stat);
    totRequired += iov[0].iov_len;

    iov[1].iov_base = &x;
    iov[1].iov_len = sizeof(x);
    totRequired += iov[1].iov_len;

    iov[2].iov_base = str;
    iov[2].iov_len = STR_SIZE;
    totRequired += iov[2].iov_len;

    numRead = readv(fd, iov, 3);
    if(numRead == -1){
        perror("readv");
        exit(1);
    }

    if(numRead < totRequired){
        printf("Read fewer bytes than requested\n");
    }

    printf("total byte requested: %ld; byte read: %ld \n", (long)totRequired, (long)numRead);

    exit(EXIT_SUCCESS);
}
```

### 集中输出

writev()系统调用实现了集中输出：将iov所指定的所有缓冲区中的数据拼接（“集中”）起来，然后以连续的字节序列写入文件描述符fd指代的文件中。对缓冲区中数据的“集中”始于iov[0]所指定的缓冲区，并按数组顺序展开。

像readv()调用一样，writev()调用也属于原子操作，即所有数据将一次性地从用户内存传输到fd指代的文件中。因此，在向普通文件写入数据时，writev()调用会把所有的请求数据连续写入文件，而不会在其他进程（或线程）写操作的影响下 分散地写入文件 。

如同write()调用，writev()调用也可能存在部分写的问题。因此，必须检查writev()调用的返回值，以确定写入的字节数是否与要求相符。

readv()调用和writev()调用的主要优势在于便捷。如下两种方案，任选其一都可替代对writev()的调用。

- 编码时，首先分配一个大缓冲区，随即再从进程地址空间的其他位置将数据复制过来，最后调用write()输出其中的所有数据。
- 发起一系列write()调用，逐一输出每个缓冲区中的数据。

尽管方案一在语义上等同于writev()调用，但需要在用户空间内分配缓冲区，进行数据复制，很不方便（效率也低）。

方案二在语义上就不同于单次的writev()调用，因为发起多次write()调用将无法保证原子性。更何况，执行一次writev()调用比执行多次write()调用开销要小（参见3.1节关于系统调用的讨论）。

### 在指定的文件偏移量处执行分散输入/集中输出

Linux 2.6.30版本新增了两个系统调用：preadv()、pwritev()，将分散输入/集中输出和于指定文件偏移量处的I/O二者集于一身。它们并非标准的系统调用，但获得了现代BSD的支持。

```c
#define _BSD_SOURCE
#include <sys/uio.h>

ssize_t preadv(int fd, const struct iovec *iov, int iovcnt, off_t offset);
				Returns number of bytes read, 0 on EOF, or -1 on error.
ssize_t pwritev(int fd, const struct iovec *iov, int iovcnt, off_t offset);
				Returns number of bytes written, or -1 on error
```



## [0x07]截断文件

## [0x08]非阻塞I/O

## [0x09]大文件I/O

## [0x0A]/dev/fd目录

## [0x0B]创建临时文件