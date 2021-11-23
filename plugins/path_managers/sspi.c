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

/**
 * List of @c sspi_interface_info objects that contain MPTCP
 * connection tokens on each network interface.
 *
 * @note We could use a map, like @c l_hashmap to map network
 *       interface to the list of tokens, but a map seems like
 *       overkill since most platforms will have very few network
 *       interfaces.
 */
static struct l_queue *sspi_interfaces;

/**
 * @struct sspi_interface_info
 *
 * @brief Network interface information.
 *
 * This plugin tracks MPTCP connection tokens on each network
 * interface.  A network interface is represented by its kernel
 * assigned index value, which is based on the local address of the
 * subflow.  Once the network interface corresponding to the subflow
 * local address is determined, the connection token for that subflow
 * is then associated with the network interface as a means to denote
 * that the MPTCP connection has a subflow on that network interface.
 */
struct sspi_interface_info
{
        /// Network interface index.
        int index;

        /**
         * @brief List of MPTCP connection tokens.
         *
         * A single network interface should have no duplicate tokens,
         * enforcing the single subflow (per connection) per network
         * interface requirement of this plugin.
         */
        struct l_queue *tokens;
};

/**
 * @struct sspi_new_connection_info
 *
 * @brief Package @c new_connection() plugin operation arguments.
 *
 * This is a convenience structure for the purpose of making it easy
 * to pass @c new_connection() plugin operation arguments through
 * a single variable.
 */
struct sspi_new_connection_info
{
        /// Network interface index.
        int index;

        /// MPTCP connection token.
        mptcpd_token_t const token;

        /// Pointer to path manager.
        struct mptcpd_pm *const pm;
};

// ----------------------------------------------------------------

/**
 * @brief Match a @c sockaddr object.
 *
 * A network address represented by @a a (@c struct @c sockaddr)
 * matches if its @c family and @c addr members match those in the
 * @a b.
 *
 * @param[in] a Currently monitored network address of type @c struct
 *              @c sockaddr*.
 * @param[in] b Network address of type @c struct @c sockaddr*
 *              to be compared against network address @a a.
 *
 * @return @c true if the network address represented by @a a matches
 *         the address @a b, and @c false otherwise.
 *
 * @see l_queue_find()
 * @see l_queue_remove_if()
 */
static bool sspi_sockaddr_match(void const *a, void const *b)
{
        struct sockaddr const *const lhs = a;
        struct sockaddr const *const rhs = b;

        assert(lhs);
        assert(rhs);
        assert(lhs->sa_family == AF_INET || lhs->sa_family == AF_INET6);

        bool matched = (lhs->sa_family == rhs->sa_family);

        if (!matched)
                return matched;

        if (lhs->sa_family == AF_INET) {
                struct sockaddr_in const *const l =
                        (struct sockaddr_in const *) lhs;
                struct sockaddr_in const *const r =
                        (struct sockaddr_in const *) rhs;

                matched = (l->sin_addr.s_addr == r->sin_addr.s_addr);
        } else {
                struct sockaddr_in6 const *const l =
                        (struct sockaddr_in6 const *) lhs;
                struct sockaddr_in6 const *const r =
                        (struct sockaddr_in6 const *) rhs;

                /**
                 * @todo Is memcmp() suitable in this case?
                 *       Do we need to worry about the
                 *       existence of uninitialized bytes in
                 *       the IPv6 address byte array.
                 */
                matched = (memcmp(&l->sin6_addr,
                                  &r->sin6_addr,
                                  sizeof(l->sin6_addr))
                                   == 0);
        }

        return matched;
}

/**
 * @brief Match a network interface index.
 *
 * @return @c true if the network interface index in the
 *         @c sspi_interface_info object @a a matches
 *         the user supplied index @a b, and @c false
 *         otherwise.
 *
 * @see l_queue_find()
 * @see l_queue_remove_if()
 */
static bool sspi_index_match(void const *a, void const *b)
{
        assert(a);
        assert(b);

        struct sspi_interface_info const *const info = a;
        int const *const index = b;

        return info->index == *index;
}

// ----------------------------------------------------------------

/**
 * @struct sspi_nm_callback_data
 *
 * @brief Type used to return index associated with local address.
 *
 * @see @c mptcpd_nm_callback
 */
struct sspi_nm_callback_data
{
        /// Local address information.        (IN)
        struct sockaddr const* const addr;

        /// Network interface (link) index.   (OUT)
        int index;
};

/**
 * @brief Get network interface index from local address.
 *
 * @see @c mptcpd_nm_callback
 */
static void sspi_get_index(struct mptcpd_interface const *interface,
                           void *data)
{
        assert(interface->index != SSPI_BAD_INDEX);
        assert(data);

        struct sspi_nm_callback_data *const callback_data = data;

        /*
          Check if the network interface index was found during an
          earlier iteration.
        */
        if (callback_data->index != SSPI_BAD_INDEX)
                return;

        /*
          Iterate through the network interface IP address list to
          determine which of them corresponds to the given IP address.
        */
        if (l_queue_find(interface->addrs,
                         sspi_sockaddr_match,
                         callback_data->addr) != NULL) {
                callback_data->index = interface->index;
        } else {
                /*
                  No network interface tracked by the mptcpd network
                  monitor with the internet address found in the
                  callback_data.
                */
                callback_data->index = SSPI_BAD_INDEX;
        }
}

/**
 * @brief Reverse lookup network interface index from IP address.
 *
 * @param[in]  nm    Mptcpd network monitor.
 * @param[in]  addr  Local address information.
 * @param[out] index Network interface (link) index.
 *
 * @return @c true if a network interface index was found that
 *         corresponds to the given local address @a addr.  @c false,
 *         otherwise.
 */
static bool sspi_addr_to_index(struct mptcpd_nm const *nm,
                               struct sockaddr const *addr,
                               int *index)
{
        assert(index != NULL);

        struct sspi_nm_callback_data data = {
                .addr = addr,
                .index = SSPI_BAD_INDEX
        };

        /**
         * @todo This iterates through the network interface list
         *       mantained by the mptcpd network monitor to find the
         *       network interface index that matches the given local
         *       address @a addr.  This could become very inefficient
         *       in the presence of a large number of network
         *       interfaces on a given platform.  Leveraging network
         *       monitor event notifications should help alleviate
         *       this issue.
         */
        mptcpd_nm_foreach_interface(nm, sspi_get_index, &data);

        *index = data.index;

        return data.index != SSPI_BAD_INDEX;
}

/**
 * @brief Compare two @c sspi_interface_info objects.
 *
 * Compare @c sspi_interface_info objects to determine where in the
 * network interface information list the first object, @a a, will be
 * inserted relative to the second object, @a b.
 *
 * @return Always returns 1 to make insertions append to the queue
 *         since there is no need to sort.
 *
 * @see l_queue_insert()
 */
static int sspi_interface_info_compare(void const *a,
                                       void const *b,
                                       void *user_data)
{
        (void) a;
        (void) b;
        (void) user_data;

        // No need to sort.
        return 1;
}

/**
 * @brief Destroy a @c sspi_interface_info object.
 *
 * @param[in,out] p Pointer to @c sspi_interface_info object.
 */
static void sspi_interface_info_destroy(void *p)
{
        if (p == NULL)
                return;

        struct sspi_interface_info *const info = p;

        l_queue_destroy(info->tokens, NULL);
        l_free(info);
}

/**
 * @brief Create a @c sspi_interface_info object.
 *
 * @param[in] index Network interface index.
 *
 * @return @c sspi_interface_info object with empty token
 *         queue.
 */
static struct sspi_interface_info *sspi_interface_info_create(int index)
{
        struct sspi_interface_info *const info =
                l_new(struct sspi_interface_info, 1);

        info->index  = index;
        info->tokens = l_queue_new();

        return info;
}

/**
 * @brief Get @c sspi_interface_info object associated with @a addr.
 *
 * @param[in] nm    Mptcpd network monitor.
 * @param[in] addr  Local address information.
 *
 * @return @c sspi_interface_info object associated with @a addr, or
 *         @c NULL if retrieval failed.
 */
static struct sspi_interface_info *sspi_interface_info_lookup(
        struct mptcpd_nm const *nm,
        struct sockaddr const *addr)
{
        assert(nm != NULL);
        assert(addr != NULL);

        /*
          Get the network interface index associated with the local
          address.

          This reverse lookup for the index is performed since
          multiple IP addresses may be associated with a single
          network interface.  As a result, a single address may not be
          enough to determine if a network interface is in use.
          Lookup the index from the local address instead.
        */
        int index;

        if (!sspi_addr_to_index(nm, addr, &index)) {
                l_error("No network interface with given IP address.");

                return NULL;
        }

        /*
          Check if a network interface with the provided local address
          (via the index value found above) is currently tracked by
          this plugin.
         */
        struct sspi_interface_info *info =
                l_queue_find(sspi_interfaces, sspi_index_match, &index);

        if (info == NULL) {
                /*
                  No MPTCP connections associated with the network
                  interface with the local address.  Prepare for
                  tracking of that network interface.
                */
                info = sspi_interface_info_create(index);

                if (!l_queue_insert(sspi_interfaces,
                                    info,
                                    sspi_interface_info_compare,
                                    NULL)) {
                        sspi_interface_info_destroy(info);
                        info = NULL;
                }
        }

        return info;
}

// ----------------------------------------------------------------

/**
 * @brief Compare two token values.
 *
 * Compare connection tokens to determine where in the token list the
 * first token, @a a, will be inserted relative to the second token,
 * @a b.
 *
 * @return Always returns 1 to make insertions append to the queue
 *         since there is no need to sort.
 *
 * @see l_queue_insert()
 */
static int sspi_token_compare(void const *a,
                              void const *b,
                              void *user_data)
{
        (void) a;
        (void) b;
        (void) user_data;

        // No need to sort.
        return 1;
}

/**
 * @brief Match MPTCP connection tokens.
 *
 * @param[in] a token (via @c L_UINT_TO_PTR()).
 * @param[in] b token (via @c L_UINT_TO_PTR()).
 *
 * @return @c true if the tokens, @a a and @b, are equal, and
 *         @c false otherwise.
 *
 * @see l_queue_find()
 * @see l_queue_remove_if()
 */
static bool sspi_token_match(void const *a, void const *b)
{
        mptcpd_token_t const lhs = L_PTR_TO_UINT(a);
        mptcpd_token_t const rhs = L_PTR_TO_UINT(b);

        return lhs == rhs;
}

/**
 * @brief Remove token from tracked network interfaces.
 *
 * @param[in] data      @c sspi_interface_info object.
 * @param[in] user_data Connection token (via @c L_UINT_TO_PTR()).
 *
 * @return @c true if @c sspi_interface_info object containing the
 *         given token was removed, and @c false otherwise.
 *
 * @see l_queue_foreach_remove()
 */
static bool sspi_remove_token(void *data, void *user_data)
{
        assert(data);
        assert(user_data);

        struct sspi_interface_info *const info = data;

        return l_queue_remove(info->tokens, user_data);
}

// ----------------------------------------------------------------

/**
 * @brief Inform kernel of local address available for subflows.
 *
 * @param[in] i    Network interface information.
 * @param[in] data User supplied data, the path manager in this case.
 */
static void sspi_send_addr(void *data, void *user_data)
{
        struct sockaddr                 const *const addr = data;
        struct sspi_new_connection_info const *const info = user_data;

        /**
         * @bug Use real values instead of these placeholders!  The
         *      @c port, in particular, is problematic because no
         *      subflows exist for the addr in question, meaning there
         *      is no port associated with it.
         */
        mptcpd_aid_t address_id = 0;

        /**
         * @note The port is an optional field of the MPTCP
         *       @c ADD_ADDR option.  Setting it to zero causes it to
         *       be ignored when sending the address information to
         *       the kernel.
         */
        /*
        in_port_t const port = 0;
        if (addr->sa_family == AF_INET)
                ((struct sockaddr_in const *) addr)->sin_port = port;
        else
                ((struct sockaddr_in6 const *) addr)->sin6_port = port;
        */

        mptcpd_pm_add_addr(info->pm,
                           addr,
                           address_id,
                           info->token);
}

/**
 * @brief Inform kernel of network interface usable local addresses.
 *
 * Send all local addresses associated with the given network
 * interface if that interface doesn't already have the initial
 * subflow on it.
 *
 * @param[in] i    Network interface information.
 * @param[in] data User supplied data, the path manager in this case.
 */
static void sspi_send_addrs(struct mptcpd_interface const *i, void *data)
{
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

        struct sspi_new_connection_info *const info = data;

        /*
          Do not reuse the network interface on which the new
          connection was created.  Only one subflow per network
          interface per MPTCP connection allowed.
        */
        if (i->index != info->index){
                /*
                  Send each address associate with the network
                  interface.
                */
                l_queue_foreach(i->addrs,
                                sspi_send_addr,
                                info);
        }
}

/********************************************************************
 *      Additional functions
 * ******************************************************************
*/


// debug functio print ipv4 addr in hex 
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

static uint32_t const max_addrs = 1;
static uint32_t const max_subflows = 1;

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

        //assert(result == 0 || result == ENOTSUP);
        if (!result)  l_info ("LIMITS CHANGED ADD_ADDR = %d , SUBFLOW = %d", 
                                                max_addrs, max_subflows); 
}

static void sspi_get_limits_callback(struct mptcpd_limit const *limits,
                                size_t len,
                                void *user_data)
{
        // uint32_t addrs_limit = max_addrs;
        // uint32_t subflows_limit = max_subflows;

        if (geteuid() != 0) {
                /*
                  if the current user is not root, the previous set_limit()
                  call is failied with ENOPERM, but libell APIs don't
                  allow reporting such error to the caller.
                  Just assume set_limits has no effect
                */
                // addrs_limit = 0;
                // subflows_limit = 0;
                l_info ("uid != 0"); 
        }

        (void) user_data;

        // assert(limits != NULL);
        // assert(len == L_ARRAY_SIZE(_limits));
        l_info("len == %zu", len);
        
        for (struct mptcpd_limit const *l = limits;
             l != limits + len; ++l) {
                if (l->type == MPTCPD_LIMIT_RCV_ADD_ADDRS) {
                        l_info ("Add limit %u", l->limit); 
                } else if (l->type == MPTCPD_LIMIT_SUBFLOWS) {
                        l_info("Sub limit: %u", l->limit);
                } else {
                        /*
                          Unless more MPTCP limit types are added to
                          the kernel path management API this should
                          never be reached.
                        */
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
                
        l_info ("Msg type %c value %f", msg->type, msg->value);

        if (in == NULL) return 0;
        struct mptcpd_pm *const pm = (struct mptcpd_pm *)in;
        
        // cmd from mptcpd to stop thread (called on Cntr+C)   
        if (msg->type == SSPI_COMM_END) return -1 ;
        
        // other cmds
        else if ( msg->type == SSPI_CMD_TEST){
                // const mptcpd_aid_t id = 2; 
                // //receive test command : send kpm_remove_addr
                // if (mptcpd_kpm_remove_addr(pm, id) != 0)
                //         l_info("Unable to stop advertising IP address.");
                // l_info ("command pass"); 
                // if (mptcpd_kpm_flush_addrs(pm) != 0)
                //         l_info("Unable to flush IP addresses.");
                // l_info ("command pass"); 
                
                sspi_set_limits (pm);

                if (mptcpd_kpm_get_limits(pm, sspi_get_limits_callback,
                                                NULL) !=0) 
                        l_info("Unable to get limits IP addresses.");
                l_info ("command pass"); 


        }  
        else{
                // just inform user, continue to reading 
                l_info("Uknown ns3 message type : %c", msg->type);
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
        // close fd when nothing to read
        close(fd);
        exit(0); 

        return EXIT_SUCCESS ; 
}
/**
 *      Get address callback
*/
static void sspi_get_addr_callback(struct mptcpd_addr_info const *info,
                              void *user_data)
{       
        // sockaddr_in
        //struct sockaddr *laddr =  (struct sockaddr*) (info->addr);
        (void) user_data;

        uint32_t flags = info->flags;
        int index = info->index ; 
        l_info ("get_addr_call : index %d , flag : %u", index, flags);  

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

       
        // unique mptcp connectioon token 
        // l_info ("token : %u", token); 

        (void) raddr;

        /**
         * @note Because we directly store connection tokens in a
         *       @c l_queue by converting them to pointers via
         *       @c L_UINT_TO_PTR(), the token cannot be zero
         *       since @c l_queue_find() returning a @c NULL pointer
         *       would be an ambiguous result.  Was a match found
         *       (zero token) or was it not found (@c NULL pointer)?
         *       The kernel always provides non-zero MPTCP connection
         *       tokens.
        */
        assert(token != 0);

        struct mptcpd_nm const *const nm = mptcpd_pm_get_nm(pm);

        struct sspi_interface_info *const interface_info =
                sspi_interface_info_lookup(nm, laddr);

        if (interface_info == NULL) {
                l_error("Unable to track new connection");

                return;
        }

        /*
          Associate the MPTCP connection with network interface
          corresponding to the local address.
         */
        if (!l_queue_insert(interface_info->tokens,
                            L_UINT_TO_PTR(token),
                            sspi_token_compare,
                            NULL)) {
                l_error("Unable to associate new token "
                        "with network interface %d",
                        interface_info->index);

                return;
        }

        /*
          Inform the kernel of additional local addresses available
          for subflows, e.g. for MP_JOIN purposes.
         */
        struct sspi_new_connection_info connection_info = {
                .index = interface_info->index,
                .token = token,
                .pm    = pm
        };

        mptcpd_nm_foreach_interface(nm,
                                    sspi_send_addrs,
                                    &connection_info);
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

        /*
          Remove all sspi_interface_info objects associated with the
          given connection token.
        */
        if (l_queue_foreach_remove(sspi_interfaces,
                                   sspi_remove_token,
                                   L_UINT_TO_PTR(token)) == 0)
                l_error("Untracked connection closed.");
}

static void sspi_new_address(mptcpd_token_t token,
                             mptcpd_aid_t id,
                             struct sockaddr const *addr,
                             struct mptcpd_pm *pm)
{
        l_info ("NEW ADDRESS: token = %u , id = %u", token, id);
        //sspi_print_sock_addr (addr); 
        
        
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
        l_info ("NEW SUBFLOW from <--> to, backup %u ", backup);
        sspi_print_sock_addr (laddr); 
        sspi_print_sock_addr (raddr); 

        (void) backup;
        
        
        
        void* data = NULL;  
        //mptcpd_kpm_get_addr(pm, 1,sspi_get_addr_callback, data); 
        mptcpd_kpm_get_addr(pm, 2,sspi_get_addr_callback, data); 

        // set to backup
        //int res = -1 ; 
        // static mptcpd_flags_t const flags = MPTCPD_ADDR_FLAG_BACKUP;
        // res = mptcpd_kpm_set_flags(pm, laddr, flags);

        // l_info("SET to backup %d", res); 
        
      
        /*
          1. Check if the new subflow local IP address corresponds to
             a network interface that already has a subflow connected
             through it, being aware that multiple IP addresses may be
             associated with a given a network interface.
          2. If the network interface corresponding to the local
             address has no subflow running on it add its connection
             token to the token list.  Otherwise, close the subflow.
         */

        struct mptcpd_nm const *const nm = mptcpd_pm_get_nm(pm);

        struct sspi_interface_info *const info =
                sspi_interface_info_lookup(nm, laddr);

        if (info == NULL) {
                l_error("Unable to track new subflow.");

                return;
        }

        if (l_queue_find(info->tokens,
                         sspi_token_match,
                         L_UINT_TO_PTR(token)) != NULL) {
                l_warn("Subflow already exists on network "
                       "interface (%d). "
                       "Closing new subflow.",
                        info->index);

                mptcpd_pm_remove_subflow(pm,
                                         token,
                                         laddr,
                                         raddr);

                return;
        }

        /*
          Associate the MPTCP subflow with network interface
          corresponding to the local address.
         */
        if (!l_queue_insert(info->tokens,
                            L_UINT_TO_PTR(token),
                            sspi_token_compare,
                            NULL))
                l_error("Unable to associate new subflow "
                        "with network interface %d",
                        info->index);
                                                
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

        /*
          1. Retrieve the subflow list associated with the local
             address.  Log an error, and return immediately if no such
             list exists.
          2. Remove the subflow information associated with the given
             local IP address from the subflow list.  Log an error,
             and return immediately if no subflow corresponding to the
             local address exists.
         */

        struct mptcpd_nm const *const nm = mptcpd_pm_get_nm(pm);

        struct sspi_interface_info *const info =
                sspi_interface_info_lookup(nm, laddr);

        if (info == NULL) {
                l_error("No tracked subflows on network interface.");

                return;
        }

        if (!l_queue_remove(info->tokens,
                            L_UINT_TO_PTR(token)))
                l_error("Closed subflow was not tracked on "
                        "network interface %d.",
                        info->index);
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

        // Create list of connection tokens on each network interface.
        sspi_interfaces = l_queue_new();

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
         * change MPTCP limits on start to allow subflow establishing 
        */
        // (void) pm;
       // __attribute__ ((unused))
        
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

        l_queue_destroy(sspi_interfaces, sspi_interface_info_destroy);

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
