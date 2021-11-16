#if !defined(MPTCPD_NS3_H)
#define MPTCPD_NS3_H

/*  FIFO NAME   */
#define SSPI_FIFO_PATH "/tmp/mptcp-ns3-fifo"

/*  MESSAGE MAX SIZE    */
int  BFSZ = 256;

/*  COMMANDS TYPES  */
// "END" cmd from mptcpd to stop thread (called on Cntr+C)
#define SSPI_COMM_END 'E'

// test cmd
#define SSPI_CMD_TEST 'T'    


/*  MESSAGE STRUCT  */
/**
 * @brief structure of message to be sent from ns-3 to mptcpd 
 * plugin 
 * 
 */
struct sspi_ns3_message
{
    char type;          // msg type 
    double value;       // msg value 
}; 

/*  MESSAGE TYPES*/

#endif // MPTCPD_NS3_H
