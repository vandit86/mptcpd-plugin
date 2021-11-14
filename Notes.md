# MPTCP userspase Path manager 

## using iptols2 
see 
```sh
ip mptcp help 
```

### endpoints

### limits 

```sh
ip mptcp help  
```

<br>
<br>

## Scenarios for subflow creation 
there are two ways of creating new subflow after MPTCP session is esteblished: 

1) ``Scenario 1`` (client send ``MP_JOIN`` immidiatly)

```sh
C   ---> (MP_JOIN)        ---> S 
C   <--- (MP_JOIN, ACK)   <--- S 
````

In this scenario additional ``route`` should be configured on cliente and routers. 
a ``subflow`` endpoint configured on the client side.

```sh
ip route add 13.0.0.0/8 via 15.0.0.1 metric 100 
```
Events on ``mptcpd`` durring connection,  

|  Client 	    |  Server 	|
|:--------:	    |:------:	|
|   NEW CONN	| CREATED  	|
|   ESTAB	    | ESTAB  	|
|   NEW_SUB	    | SF_ESTAB  |
|  ----------   | --------- | 
|   SF_CLOSED	| REMOVED   |
|               | SF_CLOSED |


2)  ``Scenario 2`` (Server send ``ADD_ADDR``)
```sh 
C   <--- (ADD_ADDR)       <--- S 
C   ---> (ADD_ADDR, ACK)  ---> S 
C   ---> (MP_JOIN)        ---> S 
C   <--- (MP_JOIN, ACK)   <--- S 
```

kernel PM will try to establish subflow when receives ADD_ADDR 

<br>
<br>


## MPTCP.v0 vs MPTCP.v1 

- ``MPTCP.v1``: 
    - isalated sysctl calls in namespaces 
    - included in upstream kernel
    - less modification to TCP stack 
    - easy mantainance 
    - intel gays  

<br>
<br>


## Testing 
- ``Evaluateing`` poposed method 
    - Transmossion time 
    - Retransmission TCP 
    - Losses 
- `File transmission`
    - different fime sizes (from 64KB to 32MB)
- `Video Streaming`
    - check video quality 
- `Evaluate parameters directly on ghost of NS-3`

<br>
<br>


## NS-3 to Namespaces Connection 

> Server (mptcpd-plugin) <-- Cliente (NS3)  

### IPC connection 
- `Named Pipes` or `System-V message`
    - [Tuttorial about FIFO config and mamny more](http://beej.us/guide/bgipc/html/single/bgipc.html#fifos)
    - Book Stivens
    -  *man msgsnd*, *man sysvipc* (`example  system-v`) <br><br> 
- `Socket TCP`

https://www.ibm.com/docs/en/ztpf/1.1.0.15?topic=considerations-unix-domain-sockets 

Ctrl-\ sends a QUIT signal (SIGQUIT); by default, this causes the process to terminate and dump core. (worked..)



Normally signals do not interrupt system calls with EINTR

https://stackoverflow.com/questions/17822025/how-to-cleanly-interrupt-a-thread-blocking-on-a-recv-call

### Message type/struct
There is an inherent message-boundary problem in any kind of stream communication, however, and you'll need to deal with it. There are several approaches; among the most commonly used are

- Fixed-length messages. The receiver can then read until it successfully transfers the required number of bytes; any blocking involved is appropriate and needful. With this approach, the scenario you postulate simply does not arise, but the writer might need to pad its messages.

- Delimited messages. The receiver then reads until it finds that it has received a message delimiter (a newline or a null byte, for example). In this case, the receiver will need to be prepared for the possibility of message boundaries not being aligned with the byte sequences transferred by read() calls. Marking the end of a message by closing the channel can be considered a special case of this alternative.

- Embedded message-length metadata. This can take many forms, but one of the simplest is to structure messages as a fixed-length integer message length field, followed by that number of bytes of message data. The reader then knows at every point how many bytes it needs to read, so it will not block needlessly. 

<br>
<br>

## Get TCP data from kernel

#### Measuring RTT
[Passively Measuring TCP Round-trip Times (Paper)](https://queue.acm.org/detail.cfm?id=2539132)  - Measuring and monitoring network RTT (round-trip time). 

1) get TCP related values using `ss` tool : 
```sh
# se all general
ss -i   

# see tcp, established 
ss -ite 'src 1.1.1.1:1234 and dst 2.2.2.2:1234'  
````

the values to be motnitored (see ``man ss`` for explanation) : 
- rcv_rtt 
- bytes_sent
- bytes_receive
- ...

2) now ``filtering output`` for required data: 

### The LD_PRELOAD trick
[hijacking](http://www.goldsborough.me/c/low-level/kernel/2016/08/29/16-48-53-the_-ld_preload-_trick/#:~:text=The%20LD_PRELOAD%20trick%20exploits%20functionality,shared%20library%20before%20other%20libraries.) of socket() system call. 

