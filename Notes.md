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

