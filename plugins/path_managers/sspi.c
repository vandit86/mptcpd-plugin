// SPDX-License-Identifier: BSD-3-Clause
/**
 * @file sspi.c
 *
 * @brief MPTCP single-subflow-per-interface path manager plugin.
 *
 * Copyright (c) 2018-2021, Intel Corporation
 */

#ifdef HAVE_CONFIG_H
# include <mptcpd/private/config.h>  // For NDEBUG and mptcpd VERSION.
#endif

#include <assert.h>
#include <stddef.h>  // For NULL.
#include <limits.h>

#include <netinet/in.h>

#include <ell/util.h>  // For L_STRINGIFY needed by l_error().
#include <ell/log.h>
#include <ell/queue.h>

#include <mptcpd/network_monitor.h>
#include <mptcpd/path_manager.h>

// #include <mptcpd/private/path_manager.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
//#include <conio.h>

#include <mptcpd/addr_info.h>

#include <mptcpd/plugin.h>

// requered for pipe fifo
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>  // exit()

#include "mptcpd/mptcp_ns3.h"

/**
 * @brief Local address to interface mapping failure value.
 */
#define SSPI_BAD_INDEX INT_MAX


/*
        struct mptcpd_interface const *i; 
        l_debug("interface\n"
                "  family: %d\n"
                "  type:   %d\n"
                "  index:  %d\n"
                "  flags:  0x%08x\n"
                "  name:   %s",
                i->family,
                i->type,
                i->index,
                i->flags,
                i->name);


static struct sockaddr const *const laddr1 =
        (struct sockaddr const *) &test_laddr_1;
static struct sockaddr_in const test_laddr_1 = {
        .sin_family = AF_INET,
        .sin_port   = 0x1234,
        .sin_addr   = { .s_addr = 0x010200C0 }  // 192.0.2.1
};

*/

/********************************************************************
 *      Additional functions
 * ******************************************************************
*/


// debug functio print ipv4 addr in hex 
__attribute__ ((unused)) 
static void sspi_print_sock_addr (struct sockaddr const *addr){
          // // print local addr (4 bytes)    
        l_info ("addr = %02X:%02X:%02X:%02X",   addr->sa_data[2], 
                                                addr->sa_data[3],
                                                addr->sa_data[4],
                                                addr->sa_data[5]
                                                );
}

/**
 *      MPTCP limits configuartion  
*/

static uint32_t const max_addrs = 2;
static uint32_t const max_subflows = 2;

static struct mptcpd_limit const _limits[] = {
        {
                .type  = MPTCPD_LIMIT_RCV_ADD_ADDRS,
                .limit = max_addrs
        },
        {
                .type  = MPTCPD_LIMIT_SUBFLOWS,
                .limit = max_subflows
        }
};

static void sspi_set_limits(void const *in)
{
        if (in == NULL) return ; 
        struct mptcpd_pm *const pm = (struct mptcpd_pm *) in;

        int const result = mptcpd_kpm_set_limits(pm,
                                                 _limits,
                                                 L_ARRAY_SIZE(_limits));

        if (!result)  l_info ("LIMITS CHANGED ADD_ADDR = %d , SUBFLOW = %d", 
                                                max_addrs, max_subflows); 
}

/* pass data with PM to callback functions */

struct sspi_pass_info
{
        struct mptcpd_pm* pm; // pm
        int data;             // data 
} pi;


/**
 *      Get address, SET FALG callback, 
 *      USED TO SET LAG ON MPTCP endpoint 
 *      Can be used during mptcp session 
 *      for example :  echo -en "\02\0\0\0\01\0\0\0\c" > /tmp/mptcp-ns3-fifo
 *      will set endpoint (id = 2) with BAKUP flag 
*/
__attribute__((unused)) 
static void sspi_set_flag_callback(struct mptcpd_addr_info const *info,
                                                           void *in)
{
        struct sspi_pass_info* pi = (struct sspi_pass_info *)in;
        struct mptcpd_pm *pm = (struct mptcpd_pm *)pi->pm;
        struct sockaddr *laddr = (struct sockaddr *)&info->addr;

        // sspi_print_sock_addr((struct sockaddr *)&info->addr);
        l_info("index = %d, id = %d, flags=%d",
               info->index, info->id, info->flags);
        // // set to backup
        
        // static mptcpd_flags_t const flags = 
        //                 (pi->data)? MPTCPD_ADDR_FLAG_BACKUP : 0 ;
        mptcpd_flags_t flags = (uint32_t) pi->data; 
        l_info("FLAG received = %d", (int) pi->data); 

        if (mptcpd_kpm_set_flags(pm, laddr, flags) != 0)
        {
                l_error("Unable to set flag %u", flags);
        }
}

static void sspi_get_limits_callback(struct mptcpd_limit const *limits,
                                size_t len,
                                void *user_data)
{
        if (geteuid() != 0) {
                /*
                  if the current user is not root, the previous set_limit()
                  call is failied with ENOPERM, but libell APIs don't
                  allow reporting such error to the caller.
                  Just assume set_limits has no effect
                */
                l_info ("uid != 0"); 
        }

        (void) user_data;

        for (struct mptcpd_limit const *l = limits;
             l != limits + len; ++l) {
                if (l->type == MPTCPD_LIMIT_RCV_ADD_ADDRS) {
                        l_info ("Add limit %u", l->limit); 
                } else if (l->type == MPTCPD_LIMIT_SUBFLOWS) {
                        l_info("Sub limit: %u", l->limit);
                } else {
                        l_error("Unexpected MPTCP limit type.");
                }
        }
}


/**
 * @brief parsing incoming msg from ns-3 
 * 
 * @param msg message to be parsed 
 * @param keep this function can change this var to true if END command is 
 * received. The main mptcpd process could send this comnd to terminate 
 * listening thread end exit.
 * @param in pointer to struct mptcpd_pm *const, need cast from void
 * 
 * @return -1 if receives "end" command from mptcpd main therad.. 
 *              0 othervise   
 */
static int sspi_msg_pars (struct sspi_ns3_message* msg, void const *in){
                
       // l_info ("Msg type %i value %f", msg->type, (double)msg->value);
        l_info ("Msg type %i value %d", msg->type, (int)msg->value);

        if (in == NULL) return 0;
        struct mptcpd_pm *const pm = (struct mptcpd_pm *)in;
        
        // cmd from mptcpd to stop thread (called on Cntr+C)   
        if (msg->type == SSPI_CMD_END) return -1 ;

        /* will be used as copy on write on other processes*/
        char buf[128];

        /* Receive Init MSG : IPC connection OK 
           START tcpdump recording durring the simulation */
        if ( msg->type == SSPI_CMD_TCPDUMP){
              l_info("Start TCPDUMP");
              if (fork() == 0)
              {
                      sprintf(buf,
                              "tcpdump -G %d -W 1 -w dump-0.pcap -i eth0",
                              msg->value);
                      l_info("%s", buf);
                      int status = system(buf);
                      exit(status);
              }
              if (fork() == 0)
              {
                      sprintf(buf,
                              "tcpdump -G %d -W 1 -w dump-1.pcap -i eth1",
                              msg->value);
                      l_info("%s", buf);
                      int status = system(buf);
                      exit(status);
              }
        }
        // remove addr 
        else if (msg->type == SSPI_CMD_DEL ){
                const mptcpd_aid_t id = 2; 
                if (mptcpd_kpm_remove_addr(pm, id) != 0)
                        l_info("Unable to remove endpoint: %d", id);
                else  
                        l_info ("Endpoint %d Removed", id); 
        }

        // set Endpoint with (id = msg-value) with backup flag
        else if (msg->type == SSPI_CMD_BACKUP_FLAG_ON){
                //  subflow ID to be changed 
                mptcpd_aid_t id = (uint8_t) msg->value;  
                // struct sspi_pass_info pi; 
                pi.pm = (struct mptcpd_pm *)in; 
                pi.data = (int) MPTCPD_ADDR_FLAG_BACKUP; // BACLUP flag

                if (mptcpd_kpm_get_addr(pm, 
                                        id,
                                        sspi_set_flag_callback, 
                                        (void *)&pi) != 0)
                {
                    l_error("Unable to get addr with id=: %d", id);
                }
        }

        // set Endpoint with (id = msg-value) with backup flag
        else if (msg->type == SSPI_CMD_CLEAR_FLAGS){
                //  subflow ID to be changed 
                mptcpd_aid_t id = (uint8_t) msg->value;  
                // struct sspi_pass_info pi; 
                pi.pm = (struct mptcpd_pm *)in; 
                pi.data = (int) 0;          // CLEAR all flags

                if (mptcpd_kpm_get_addr(pm, 
                                        id,
                                        sspi_set_flag_callback, 
                                        (void *)&pi) != 0)
                {
                    l_error("Unable to get addr with id=: %d", id);
                }
        }

        else if (msg->type == SSPI_CMD_WIFI_SNR){

                l_info ("SNR : %d", msg->value); 
                // if ( mptcpd_kpm_dump_addrs(pm, 
                //         sspi_set_flag_callback, (void*)in) !=0){
                //                 l_error ("Unable dump adrese"); 
                //         }
        }

        // stert generate traffic in separate process 
        else if (msg->type == SSPI_CMD_IPERF_START){
                
                if (fork() == 0){
                        sprintf(buf,
                                "/home/vad/mptcp-tools/use_mptcp/use_mptcp.sh iperf -c 13.0.0.2 -e -i1 -t %d",
                                msg->value);
                        l_info("%s",buf); 
                        int status = system(buf);
                        // maybe should try with execl () instead of system()
                        // execl("/path/to/foo", "foo", "arg1", "arg2", "arg3", 0);
                        exit(status);  
                }

        }  
        else{
                // just inform user, continue to reading 
                l_info("Uknown ns3 message : %d", (int)msg->type);
        }
      
        return EXIT_SUCCESS; 
}

/**
 * @brief starts listeng thread. Listeng for upcoming commands from NS-3
 *  
 * @param in mptcpd_pm path manager  
 */

static void* sspi_connect_pipe(void *in)
{
        if (in == NULL) EXIT_FAILURE; // path manager
        
        // struct mptcpd_pm *const pm = (struct mptcpd_pm *)in;

        int fd;
        struct sspi_ns3_message msg;

        /* Creating the named file(FIFO) */
        unlink(SSPI_FIFO_PATH);
        mkfifo(SSPI_FIFO_PATH, 0666);

        /* non blocking syscall open() */
        fd = open(SSPI_FIFO_PATH, O_RDWR);

        if (fd < 0)
                exit(1); // check fd

        /* maybe it's better to use poll() for non bloking */
        ssize_t nb = 0; // num of bytes readed
        l_info("listening thead..");
        while ((nb = read(fd, &msg, sizeof(msg))) > 0)
        {
                /* now parsing msg data                 */
                /* read until receive stop command      */
                l_info("Received: %lu bytes \n", nb);
                
                // receive "end" command
                if (sspi_msg_pars(&msg, in) < 0)
                        break;
        }
        // close fd when nothing to read end exit the thread 
        //close(fd);
        l_info("Exit Reading thread");
        return EXIT_SUCCESS ; 
}


// ----------------------------------------------------------------
//                     Mptcpd Plugin Operations
// ----------------------------------------------------------------
static void sspi_new_connection(mptcpd_token_t token,
                                struct sockaddr const *laddr,
                                struct sockaddr const *raddr,
                                struct mptcpd_pm *pm)
{
        l_info ("NEW CONNECTION : mptcp token = %u ", token); 

        (void) token;
        (void) laddr;
        (void) raddr;
        (void) pm;
      
}

static void sspi_connection_established(mptcpd_token_t token,
                                        struct sockaddr const *laddr,
                                        struct sockaddr const *raddr,
                                        struct mptcpd_pm *pm)
{
        l_info ("CONNECTION ESTABLISHED"); 
        (void) token;
        (void) laddr;
        (void) raddr;
        (void) pm;

        /**
         * @todo Implement this function.
         */
       // l_warn("%s is unimplemented.", __func__); 
}

static void sspi_connection_closed(mptcpd_token_t token,
                                   struct mptcpd_pm *pm)
{
        l_info ("CONNECTION CLOSED"); 
        (void) pm;
        (void) token; 

        /*
          Remove all sspi_interface_info objects associated with the
          given connection token.
        */
      
}

static void sspi_new_address(mptcpd_token_t token,
                             mptcpd_aid_t id,
                             struct sockaddr const *addr,
                             struct mptcpd_pm *pm)
{
        l_info ("NEW ADD_ADDR: token = %u , id = %u", token, id);
        (void) token;
        (void) id;
        (void) addr;
        (void) pm;

        /*
          The sspi plugin doesn't do anything with newly advertised
          addresses.
        */
}

static void sspi_address_removed(mptcpd_token_t token,
                                 mptcpd_aid_t id,
                                 struct mptcpd_pm *pm)
{
        l_info ("ADDR REMOVED");
        (void) token;
        (void) id;
        (void) pm;

        /*
          The sspi plugin doesn't do anything with addresses that are
          no longer advertised.
        */
}

static void sspi_new_subflow(mptcpd_token_t token,
                             struct sockaddr const *laddr,
                             struct sockaddr const *raddr,
                             bool backup,
                             struct mptcpd_pm *pm)
{
        l_info ("NEW SUBFLOW local <--> remote, backup %u ", backup);
        sspi_print_sock_addr (laddr); 
        sspi_print_sock_addr (raddr);
        
        (void) pm;  
        (void) backup;
        (void) token;                                                 
}

static void sspi_subflow_closed(mptcpd_token_t token,
                                struct sockaddr const *laddr,
                                struct sockaddr const *raddr,
                                bool backup,
                                struct mptcpd_pm *pm)
{
        l_info ("SUBFLOW CLOSED");
        (void) raddr;
        (void) backup;
        (void) laddr; 
        (void) pm; 
        (void) token; 
}


static void sspi_subflow_priority(mptcpd_token_t token,
                                  struct sockaddr const *laddr,
                                  struct sockaddr const *raddr,
                                  bool backup,
                                  struct mptcpd_pm *pm)
{
        l_info ("SUBFLOW PRIORITY");
        (void) token;
        (void) laddr;
        (void) raddr;
        (void) backup;
        (void) pm;
        
        /*
            The sspi plugin doesn't do anything with changes in subflow
            priority.
        */
}

/**
 *      network monitor event handlers 
*/

static void sspi_new_interface (struct mptcpd_interface const *i,
                                struct mptcpd_pm *pm){

        l_info ("NEW INTERFACE"); 
        (void) i; 
        (void) pm; 
}

static void sspi_update_interface (struct mptcpd_interface const *i,
                         struct mptcpd_pm *pm)
{
        l_info ("UPDATE interface flags");
        (void) i; 
        (void) pm;  
}

static void sspi_delete_interface(struct mptcpd_interface const *i,
                                  struct mptcpd_pm *pm)
{
        l_info("INTEFACE REMOVED");
        (void) i; 
        (void) pm; 
}

static void sspi_new_local_address(struct mptcpd_interface const *i,
                                   struct sockaddr const *sa,
                                   struct mptcpd_pm *pm)
{
        l_info("NEW LOCAL ADDR");
        (void)i;
        (void)sa;
        (void)pm;
}

static void sspi_delete_local_address(struct mptcpd_interface const *i,
                                      struct sockaddr const *sa,
                                      struct mptcpd_pm *pm)
{
        l_info("NET ADDR removed");
        (void)i;
        (void)sa;
        (void)pm;
}

static struct mptcpd_plugin_ops const pm_ops = {
        .new_connection         = sspi_new_connection,
        .connection_established = sspi_connection_established,
        .connection_closed      = sspi_connection_closed,
        .new_address            = sspi_new_address,
        .address_removed        = sspi_address_removed,
        .new_subflow            = sspi_new_subflow,
        .subflow_closed         = sspi_subflow_closed,
        .subflow_priority       = sspi_subflow_priority,
        // network monitor event handler 
        .new_interface          = sspi_new_interface,       
        .update_interface       = sspi_update_interface,
        .delete_interface       = sspi_delete_interface,
        .new_local_address      = sspi_new_local_address,
        .delete_local_address   = sspi_delete_local_address
};

static int sspi_init(struct mptcpd_pm *pm)
{
        l_warn ("INIT PM");

      

        static char const name[] = "sspi";

        if (!mptcpd_plugin_register_ops(name, &pm_ops)) {
                l_error("Failed to initialize "
                        "single-subflow-per-interface "
                        "path manager plugin.");

                return -1;
        }

        l_info("MPTCP single-subflow-per-interface "
               "path manager initialized.");

        /**
         * get NM 
         * struct mptcpd_nm const *const nm = mptcpd_pm_get_nm(pm);
         * 
         * mptcpd_nm_foreach_interface(nm,
                                    sspi_send_addrs,
                                    &connection_info);
         * 
         */


        /**
         * ELL data struct (see ell/unit)
         *  
         * l_hashmap
         * 
         * l_queue
         *      // Create list of 
         *      static struct l_queue *sspi_interfaces;
                sspi_interfaces = l_queue_new();
         *      l_queue_foreach(i->addrs, sspi_send_addr, info);
                
                l_queue_foreach_remove(sspi_interfaces,
                                   sspi_remove_token,
                                   L_UINT_TO_PTR(token))

         *      l_queue_remove(info->tokens, user_data);
         *      l_queue_remove_if()
         *      l_queue_insert(sspi_interfaces, info,
                                    sspi_interface_info_compare,
                                    NULL); 
                l_queue_find(sspi_interfaces, sspi_index_match, &index);
                l_queue_destroy(info->tokens, NULL);
                l_queue_destroy(sspi_interfaces, 
                                sspi_interface_info_destroy);
                l_free(info);
         * 
         * l_uintset
         * l_ringbuf
         * 
         * ELL Utils
         * l_getrandom 
         * 
         * 
        */
        // (void) pm;
       // __attribute__ ((unused))

        sspi_set_limits(NULL);
        if (mptcpd_kpm_get_limits(pm, sspi_get_limits_callback,
                                  NULL) != 0)
            l_info("Unable to get limits IP addresses.");
        
        
        /**
         * create separate thread to listening ingomming data,
         * for example from NS3
         */

        pthread_t thread;
        int status;
        status = pthread_create(&thread, NULL, sspi_connect_pipe, 
                (void*) pm);
        if (status != 0)
        {
                l_info("Plugin, can't create thread");
                exit(EXIT_FAILURE);
        }

        return 0;
}

static void sspi_exit(struct mptcpd_pm *pm)
{
        l_info ("EXIT"); 
        (void) pm;

        

        l_info("MPTCP single-subflow-per-interface path manager exited.");
}

MPTCPD_PLUGIN_DEFINE(sspi,
                     "Single-subflow-per-interface path manager",
                     MPTCPD_PLUGIN_PRIORITY_DEFAULT,
                     sspi_init,
                     sspi_exit)


/*
  Local Variables:
  c-file-style: "linux"
  End:
*/
