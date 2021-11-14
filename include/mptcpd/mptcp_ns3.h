#if !defined(MPTCPD_NS3_H)
#define MPTCPD_NS3_H

#define SSPI_COMM_END "end"
/*  FIFO NAME   */
const char *myfifo = "/tmp/myfifo";

/*  MESSAGE MAX SIZE    */
int  BFSZ = 256;

/*  MESSAGE STRUCT  */

struct sspi_ns3_message
{
    char type;
    double value;  

}; 

/*  MESSAGE TYPES*/

#endif // MPTCPD_NS3_H
