## Mayquill
**WIP**\
This is both a wayland-scanner, and libwayland alternative, specifically for servers. It uses static dispatch, rather than dynamic dispatch, hence why the scanner and wire are coupled.

For a real example on how to use it, see [Mayday](https://github.com/Mayware/mayday). Alternatively, the architecture below explains how to use it.

## Architecture
handle_* functions are customisation points / hooks. For example, you may do Client::handle_destroy(), to run code before the real destruction of the client occurs. Equally, you may have WlSurface::handle(Request request), where request is an algebraic enum of all the possible request datas that you can switch on (and hence determine what actual request occured).

The client has the following configuration points:\
`handle_init()`\
`handle_destroy()`

Individual wayland objects have the following configuration points:\
`handle(Request request)`\
`handle_destroy()`

They have default implementations, to prevent compile time errors (using gnu::weak), and hence, are overrideable.\
To override, the TU must be part of the mayquill module, so it can define a new implementation in it.\
Internally, the client, each wayland object, server, etc, are all just module partitions of mayquill.

Here's an example flow:\
Create the server, `bind_socket()`, and `try_accept_clients()`. If there are connection(s) waiting, a new client object is initialised on the heap and put into a unique_ptr. We then create the default `WlDisplay` object on it with object_id of 1. Enquue the client into the clients vector. The memory address of that client will never change, it is essentially a stable identifier.

Now `try_listen_requests()`. We iterate over each client, and try read from the socket. If there's data it's added to the `requests_data`, and the same for fds. We read the `sizeof(waylandheader)` from the start of the `requests_data`. If the `header.size` <= the current size of `requests_data` (ie. we atleast have received a whole message), we memcpy that message, and erase it from the `requests_data` and send it off to `client.process_request()`. `process_request()` extracts the object id from the header, and picks the matching object concretely from its `objects` vector. Each object has a Request algebraic enum inside which contains all the possible request structs. Eg.
```c++
using Request = std::variant<Destroy, DestroyRegistry, AckGlobalRemove>; // Destroy is opcode 0, DestroyRegistry is opcode 1, etc
```
Where for example, `AckGlobalRemove` is:
```c++
struct [[=WlDeclaration::None]]
AckGlobalRemove {
    [[=WlType::Object]]
    std::uint32_t registry;

    [[=WlType::Uint]]
    std::uint32_t name;
};
```
The opcode is matched to the request struct. This struct type is then passed into `deserialise_struct()`. Notice each field also has an attribute describing its wayland type. `deserialise_struct()` in combination with `deserialise_field()` uses this information to construct that request struct, from the message data, programmatically (including fds). The `handle()` function is then called for that wayland object, and the constructed request struct is then passed into the `handle()` function. If that request itself has an additional annotation of `WlDeclaration::Destructor`, then the `.destroy()` is called upon that object, after `handle()`. `.destroy()` in turn calls `handle_destroy()` (a hook you can change), and then calls `client.remove_object(this)`, which actually removes that object.

Now `try_flush_events()`. Whenever you call an event on a wayland object (just ordinary functions), it serialises the parameters you give it into a bytestream (matches wayland attributes, because of annotations). The function, eg. `wl_touch.frame()` calls `client.process_event()`, giving it the parameters and opcode. The client will then, using `serialise_field()`, serialise that programmatically into `event_data` and `event_fds`, which are just `vec<uint8>`. Message boundaries are not explicitly preserved, becuase the clients reform that like we do when we receive requests. When `try_flush_events()` is called, we then try send all the pre-existing `event_data` and `event_fds` to the client, using `flush_events()`.

##  Licensing
The project's source code is licensed under `LGPL-3.0-or-later`.

The branding (eg. project name, logos etc.) is not covered by the aforementioned license. Reasonable descriptive use (eg. packaging, articles, etc.) is completely fine.
