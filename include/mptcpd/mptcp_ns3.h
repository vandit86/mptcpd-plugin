#if !defined(MPTCPD_NS3_H)
#define MPTCPD_NS3_H

/*  FIFO NAME   */
#define SSPI_FIFO_PATH "/tmp/mptcp-ns3-fifo"

/*  MESSAGE MAX SIZE    */
int  BFSZ = 256;

/*  COMMANDS TYPES  */
/* 
    write type=2 , value = 2 (int.int)
    echo -en "\02\0\0\0\02\0\0\0\c" > /tmp/mptcp-ns3-fifo
*/
enum{
    SSPI_CMD_TEST=0,    // \00 test command
    SSPI_CMD_DEL,       // \01 delete one path (id in value)
    SSPI_CMD_FLAG,      // \02 flag set to backup
    SSPI_CMD_DUMP,      // \03 dump addreses mptcp 
    SSPI_COMM_END,      // \ stop receiving tread on mptcpd
    SSPI_CMD_LAST       // last command value 
}; 

/*  MESSAGE STRUCT  */
/**
 * @brief structure of message to be sent from ns-3 to mptcpd 
 * plugin 
 * todo : view padding, maybe put char type oin the end of struct
 * https://stackoverflow.com/questions/2748995/struct-memory-layout-in-c/2749096#:~:text=Struct%20members%20are%20stored%20in,of%20sizeof(T)%20bytes.
 * 
 */
struct sspi_ns3_message
{
    int type;           // msg type (padding is 4)
    int value;          // msg value 
    //long long ll;     // @ 8 bytes
    //char type;        // @ 1 (msg type) 
}; 

/*  MESSAGE TYPES*/

#endif // MPTCPD_NS3_H
