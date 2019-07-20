//  BC95G Network Transport for Apache Mynewt.  This provides the OIC (Open Interconnect Consortium)
//  interface for the BC95G Driver, so that we may compose and transmit CoAP requests using Mynewt's
//  OIC implementation.  More about Mynewt OIC: https://mynewt.apache.org/latest/os/modules/devmgmt/newtmgr.html
#include <os/os.h>
#include <console/console.h>
#include <sensor_network/sensor_network.h>
#include "util.h"
#include "bc95g/bc95g.h"
#include "bc95g/transport.h"

static void oc_tx_ucast(struct os_mbuf *m);
static uint8_t oc_ep_size(const struct oc_endpoint *oe);
static int oc_ep_has_conn(const struct oc_endpoint *);
static char *oc_ep_str(char *ptr, int maxlen, const struct oc_endpoint *);
static int oc_init(void);
static void oc_shutdown(void);
//  static void oc_event(struct os_event *ev);

static const char *network_device;     //  Name of the BC95G device that will be used for transmitting CoAP messages e.g. "bc95g_0" 
static struct bc95g_server *server;    //  CoAP Server host and port.  We only support 1 server.
static bc95g_socket *socket;           //  Reusable UDP socket connection.
static uint8_t transport_id = -1;      //  Will contain the Transport ID allocated by Mynewt OIC.

//  Definition of BC95G driver as a transport for CoAP.  Only 1 BC95G driver instance supported.
static const struct oc_transport transport = {
    0,               //  uint8_t ot_flags;
    oc_ep_size,      //  uint8_t (*ot_ep_size)(const struct oc_endpoint *);
    oc_ep_has_conn,  //  int (*ot_ep_has_conn)(const struct oc_endpoint *);
    oc_tx_ucast,     //  void (*ot_tx_ucast)(struct os_mbuf *);
    NULL,  //  void (*ot_tx_mcast)(struct os_mbuf *);
    NULL,  //  enum oc_resource_properties *ot_get_trans_security)(const struct oc_endpoint *);
    oc_ep_str,    //  char *(*ot_ep_str)(char *ptr, int maxlen, const struct oc_endpoint *);
    oc_init,      //  int (*ot_init)(void);
    oc_shutdown,  //  void (*ot_shutdown)(void);
};

int bc95g_register_transport(const char *network_device0, struct bc95g_server *server0, const char *host, uint16_t port) {
    //  Register the BC95G device as the transport for the specifed CoAP server.  
    //  network_device is the BC95G device name e.g. "bc95g_0".  Return 0 if successful.
    assert(network_device0);  assert(server0);

    {   //  Lock the BC95G driver for exclusive use.  Find the BC95G device by name.
        struct bc95g *dev = (struct bc95g *) os_dev_open(network_device0, OS_TIMEOUT_NEVER, NULL);  //  BC95G_DEVICE is "bc95g_0"
        assert(dev != NULL);

        //  Register BC95G with Mynewt OIC to get Transport ID.
        transport_id = oc_transport_register(&transport);
        assert(transport_id >= 0);  //  Registration failed.

        //  Init the server endpoint before use.
        int rc = init_bc95g_server(server0, host, port);
        assert(rc == 0);

        //  Connect to NB-IoT network.  This may take a while to complete (or fail), thus we
        //  need to run this in the Network Task in background.  The Main Task will run the Event Loop
        //  to pass BC95G events to this function.
        rc = bc95g_connect(dev);
        assert(rc == 0);

        //  Allocate a new UDP socket.
        rc = bc95g_socket_open(dev, &socket);
        assert(rc == 0);

        //  BC95G registered.  Remember the details.
        network_device = network_device0;
        server = server0;

        //  Close the BC95G device when we are done.
        os_dev_close((struct os_dev *) dev);
        //  Unlock the BC95G driver for exclusive use.
    }
    return 0;
}

int init_bc95g_server(struct bc95g_server *server, const char *host, uint16_t port) {
    //  Init the server endpoint before use.  Returns 0.
    int rc = init_bc95g_endpoint(&server->endpoint, host, port);  assert(rc == 0);
    server->handle = (struct oc_server_handle *) server;
    return 0;
}

int init_bc95g_endpoint(struct bc95g_endpoint *endpoint, const char *host, uint16_t port) {
    //  Init the endpoint before use.  Returns 0.
    assert(transport_id >= 0);  //  Transport ID must be allocated by OIC.
    endpoint->ep.oe_type = transport_id;  //  Populate our transport ID so that OIC will call our functions.
    endpoint->ep.oe_flags = 0;
    if (host) { 
        endpoint->host = host;
        endpoint->port = port;
    }
    return 0;
}

///////////////////////////////////////////////////////////////////////////////
//  OIC Callback Functions

static void oc_tx_ucast(struct os_mbuf *m) {
    //  Transmit the chain of mbufs to the network over UDP.  First mbuf is CoAP header, remaining mbufs contain the CoAP payload.

    //  Find the endpoint header.  Should be the end of the packet header of the first packet.
    assert(m);  assert(OS_MBUF_USRHDR_LEN(m) >= sizeof(struct bc95g_endpoint));
    struct bc95g_endpoint *endpoint = (struct bc95g_endpoint *) OC_MBUF_ENDPOINT(m);

    assert(endpoint);  assert(endpoint->host);  assert(endpoint->port);  //  Host and endpoint should be in the endpoint.
    assert(server);  assert(endpoint->host == server->endpoint.host);  assert(endpoint->port == server->endpoint.port);  //  We only support 1 server connection. Must match the message endpoint.
    assert(network_device);  assert(socket);
    int rc;

    {   //  Lock the BC95G driver for exclusive use.  Find the BC95G device by name.
        struct bc95g *dev = (struct bc95g *) os_dev_open(network_device, OS_TIMEOUT_NEVER, NULL);  //  BC95G_DEVICE is "bc95g_0"
        assert(dev != NULL);
        console_printf("NBT send udp\n");

        //  Send the consolidated buffer via UDP.
        rc = bc95g_socket_tx_mbuf(dev, socket, endpoint->host, endpoint->port, m);
        assert(rc > 0);

        //  Close the BC95G device when we are done.
        os_dev_close((struct os_dev *) dev);
        //  Unlock the BC95G driver for exclusive use.
    }

    //  After sending, free the chain of mbufs.
    rc = os_mbuf_free_chain(m);  assert(rc == 0);
}

static uint8_t oc_ep_size(const struct oc_endpoint *oe) {
    //  Return the size of the endpoint.  OIC will allocate space to store this endpoint in the transmitted mbuf.
    return sizeof(struct bc95g_endpoint);
}

static int oc_ep_has_conn(const struct oc_endpoint *oe) {
    //  Return true if the endpoint is connected.  We always return false.
    //  console_printf("oc_ep_has_conn\n");
    return 0;
}

static char *oc_ep_str(char *ptr, int maxlen, const struct oc_endpoint *oe) {
    //  Log the endpoint message.
    console_printf("NBT str\n");
#ifdef NOTUSED
    const struct oc_endpoint_ip *oe_ip = (const struct oc_endpoint_ip *)oe;
    int len;
    mn_inet_ntop(MN_PF_INET, oe_ip->v4.address, ptr, maxlen);
    len = strlen(ptr);
    snprintf(ptr + len, maxlen - len, "-%u", oe_ip->port);
    return ptr;
#endif  //  NOTUSED
    strcpy(ptr, "TODO:oc_ep_str");
    return ptr;
}

static int oc_init(void) {
    //  Init the endpoint.
    //  console_printf("oc_init\n");
    return 0;
}

static void oc_shutdown(void) {
    //  Shutdown the endpoint.
    //  console_printf("oc_shutdown\n");
}
