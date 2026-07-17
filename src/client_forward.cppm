export module mayquill:client_forward;

/*  This is to mainly document the module architecture in source.
 *
 *  Every wayland object needs access to the client, since they will need to tell if they have errored
 *  or if they need to be destroyed etc.
 *
 *  However, the interface (ie. the variant of all the wayland objects), imports all of these wayland objects.
 *  The client then also imports the interface, and hence the wayland objects, causing a cycle. The objects
 *  require the client, and the client requires the objects (indirectly through interface). That was the issue
 *  with the previous architecture.
 *  
 *  With the new architecture, we forward declare the client struct, which the wayland objects can use. This
 *  allows us to do Client*. Obviously, we can't access fields of it, since it's just struct Client*, so we
 *  must remove the implementations of the functions (eg. ones that use deserialise, and hence require the
 *  client fd, and thus the concrete client) into a module partition. This module partition can then import
 *  the full fat client without any issues, because the client doesn't import the implementation, only the
 *  signature from the wayland object partition interface unit. Hence, the cycle is solved.
 */

export namespace mayquill {
    struct Client;
}
