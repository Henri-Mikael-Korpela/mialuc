/// Access to the network of the system, with TCP sockets over IPv4: a program listens on a port,
/// accepts the connections that clients make, and receives and sends the bytes of each. It works on
/// Windows only for now, and is as thin as the system is: a `Socket` is the handle that the system
/// gives, with no buffer of its own and no lock, and every procedure takes the memory that it needs
/// from its caller, so nothing here allocates, and nothing is shared between threads unless the
/// caller shares it. The structures that Winsock takes, `WSADATA` and `sockaddr_in`, are arrays of
/// bytes that are filled in here, so that the programs that use sockets do not have to.
package mu

import foreign ws2_32 "system:Ws2_32.lib"

/// The procedures of `Ws2_32.dll`, which the program imports by name and the loader of Windows
/// resolves when the program starts. A `SOCKET` is the size of an `Int`, an `int` is an `I32`, and
/// the addresses and the buffers are `^Byte`, as the structures are bytes.
foreign ws2_32 #(default_calling_convention("system")) ->
    WSAGetLastError :: proc() I32 -> ...
    WSAStartup      :: proc(version: U32, data: ^Byte) I32 -> ...
    socket          :: proc(family: I32, kind: I32, protocol: I32) Int -> ...
    setsockopt      :: proc(handle: Int, level: I32, name: I32, value: ^Byte, length: I32) I32 -> ...
    bind            :: proc(handle: Int, address: ^Byte, length: I32) I32 -> ...
    listen          :: proc(handle: Int, backlog: I32) I32 -> ...
    accept          :: proc(handle: Int, address: ^Byte, length: ^Byte) Int -> ...
    recv            :: proc(handle: Int, buffer: ^Byte, length: I32, flags: I32) I32 -> ...
    send            :: proc(handle: Int, buffer: ^Byte, length: I32, flags: I32) I32 -> ...
    shutdown        :: proc(handle: Int, how: I32) I32 -> ...
    closesocket     :: proc(handle: Int) I32 -> ...

/// The version of Winsock that is asked for, 2.2, as the `WORD` that `MAKEWORD(2, 2)` makes.
WINSOCK_VERSION : U32 : 0x0202

/// `WSADATA` is 408 bytes on x64. 512 is enough, and the system writes it.
WSADATA_BYTES :: 512

/// `sockaddr_in` is 16 bytes: the family in 2 bytes, the port in 2 bytes with the high one first,
/// the address in 4 bytes, and 8 bytes that are not used.
SOCKADDR_IN_BYTES :: 16
SOCKADDR_IN_PORT :: 2
SOCKADDR_IN_ADDRESS :: 4

/// The bytes of an IPv4 address, such as the 4 of 127.0.0.1.
IPV4_ADDRESS_BYTES :: 4

AF_INET : I32 : 2
SOCK_STREAM : I32 : 1
IPPROTO_TCP : I32 : 6
SOL_SOCKET : I32 : 0xFFFF
SO_RCVTIMEO : I32 : 0x1006
SD_SEND : I32 : 1

/// What the system gives for a socket that could not be made or accepted. The handle is -1, which is
/// not a handle that a socket has.
INVALID_SOCKET_VALUE :: -1

WSAEACCES : I32 : 10013
WSAEADDRINUSE : I32 : 10048
WSAECONNRESET : I32 : 10054
WSAETIMEDOUT : I32 : 10060
WSAENOTSOCK : I32 : 10038

/// An open socket: the handle of the system. A socket that is not open has the handle -1. It is
/// copied by copying the handle, so a copy is the same socket, and the socket is closed once. A
/// socket that listens is made with `net_listen`, and one that is connected to a client with
/// `net_accept`.
Socket :: struct ->
    handle: Int

/// Why a procedure of the network did not do what it was asked. NONE is that it did.
Net_Error :: enum Int ->
    NONE
    /// The address is not 4 bytes, or the port is not from 1 to 65535.
    ADDRESS_INVALID
    /// Another program listens on the port already.
    ADDRESS_IN_USE
    /// The system does not let the program do it.
    ACCESS_DENIED
    /// Winsock could not be started.
    START_FAILED
    /// The other end did not send in the time that `net_set_receive_timeout` gave it.
    TIMED_OUT
    /// The other end closed the connection abruptly.
    CONNECTION_RESET
    /// The socket given is not an open one, or is one that was closed.
    SOCKET_INVALID
    /// Anything else that the system says.
    OTHER

/// Gives the error that the code of Winsock, which `WSAGetLastError` gives, is.
net_error_from :: proc(code: I32) Net_Error ->
    match code ->
        case WSAEADDRINUSE ->
            return .ADDRESS_IN_USE
        case WSAEACCES ->
            return .ACCESS_DENIED
        case WSAETIMEDOUT ->
            return .TIMED_OUT
        case WSAECONNRESET ->
            return .CONNECTION_RESET
        case WSAENOTSOCK ->
            return .SOCKET_INVALID
        case ->
            return .OTHER

/// Makes the socket that listens on `port` of the address `host`, which is its 4 bytes, such as
/// 127.0.0.1, and gives it with `Net_Error.NONE`, or a socket that is not open with the error. At
/// most `backlog` connections wait to be accepted. Winsock is started by the first call. The socket
/// is closed with `net_close`.
net_listen :: proc(host: []Byte, port: Int, backlog: Int) (Socket, Net_Error) ->
    closed := Socket { handle = INVALID_SOCKET_VALUE }
    if length_of(host) not= IPV4_ADDRESS_BYTES or port < 1 or port > 65535 ->
        return closed, .ADDRESS_INVALID

    data := [WSADATA_BYTES]Byte { .. = 0 }
    if WSAStartup(WINSOCK_VERSION, %data[0]) not= 0 ->
        return closed, .START_FAILED

    handle := socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)
    if handle == INVALID_SOCKET_VALUE ->
        return closed, net_error_from(WSAGetLastError())

    // MISSING: a struct of the layout of `sockaddr_in`, and `htons`, so the family and the port,
    // which has the high byte first, are put in the bytes by hand.
    address := [SOCKADDR_IN_BYTES]Byte { .. = 0 }
    address[0] = int_to_byte(cast(Int)AF_INET)
    address[SOCKADDR_IN_PORT] = int_to_byte(port / 256)
    address[SOCKADDR_IN_PORT + 1] = int_to_byte(port - port / 256 * 256)
    for index in 0..<IPV4_ADDRESS_BYTES ->
        address[SOCKADDR_IN_ADDRESS + index] = host[index]
    if bind(handle, %address[0], int_to_i32(SOCKADDR_IN_BYTES)) not= 0 ->
        error := net_error_from(WSAGetLastError())
        closesocket(handle)
        return closed, error
    if listen(handle, int_to_i32(backlog)) not= 0 ->
        error := net_error_from(WSAGetLastError())
        closesocket(handle)
        return closed, error
    return Socket { handle = handle }, .NONE

/// Waits for a client to connect to `listener`, and gives the socket that is connected to it with
/// `Net_Error.NONE`, or a socket that is not open with the error. The socket is closed with
/// `net_close`.
net_accept :: proc(listener: Socket) (Socket, Net_Error) ->
    closed := Socket { handle = INVALID_SOCKET_VALUE }
    if listener.handle == INVALID_SOCKET_VALUE ->
        return closed, .SOCKET_INVALID
    handle := accept(listener.handle, nil, nil)
    if handle == INVALID_SOCKET_VALUE ->
        return closed, net_error_from(WSAGetLastError())
    return Socket { handle = handle }, .NONE

/// Sets how long a receive of `connection` waits for the other end to send, in milliseconds, so
/// that a client that does not send does not hold the program. A receive that waited that long
/// without a byte is `Net_Error.TIMED_OUT`. Zero is to wait without end, which is what a socket does
/// when it is made.
net_set_receive_timeout :: proc(connection: Socket, milliseconds: Int) Net_Error ->
    if connection.handle == INVALID_SOCKET_VALUE ->
        return .SOCKET_INVALID
    // The timeout is a `DWORD` of 4 bytes, the lowest first. MISSING: the sizes of integers in
    // memory, so that a pointer to an `I32` or a `U32` is a pointer to an `int`.
    timeout := [4]Byte { .. = 0 }
    rest := milliseconds
    for index in 0..<4 ->
        quotient := rest / 256
        timeout[index] = int_to_byte(rest - quotient * 256)
        rest = quotient
    if setsockopt(connection.handle, SOL_SOCKET, SO_RCVTIMEO, %timeout[0], 4) not= 0 ->
        return net_error_from(WSAGetLastError())
    return .NONE

/// Receives from `connection` into `buffer`, which the caller owns, as many bytes as the system
/// gives at once, which is at most as many as the buffer has, and gives how many with
/// `Net_Error.NONE`. Zero bytes is that the other end closed the connection. A buffer with no bytes
/// asks for none and gets none.
net_receive :: proc(connection: Socket, buffer: []Byte) (Int, Net_Error) ->
    if connection.handle == INVALID_SOCKET_VALUE ->
        return 0, .SOCKET_INVALID
    if length_of(buffer) == 0 ->
        return 0, .NONE
    count := recv(connection.handle, %buffer[0], int_to_i32(length_of(buffer)), 0)
    if count < 0 ->
        return 0, net_error_from(WSAGetLastError())
    return cast(Int)count, .NONE

/// Sends `buffer`, which the caller owns, to `connection`, as many bytes as the system takes at
/// once, which is at most as many as the buffer has, and gives how many with `Net_Error.NONE`. A
/// buffer with no bytes sends none.
net_send :: proc(connection: Socket, buffer: []Byte) (Int, Net_Error) ->
    if connection.handle == INVALID_SOCKET_VALUE ->
        return 0, .SOCKET_INVALID
    if length_of(buffer) == 0 ->
        return 0, .NONE
    count := send(connection.handle, %buffer[0], int_to_i32(length_of(buffer)), 0)
    if count < 0 ->
        return 0, net_error_from(WSAGetLastError())
    return cast(Int)count, .NONE

/// Sends all of `buffer` to `connection`, in as many parts as the system takes it in, and gives
/// `Net_Error.NONE` when it took all of it. A system that takes no byte is `Net_Error.OTHER`.
net_send_all :: proc(connection: Socket, buffer: []Byte) Net_Error ->
    sent := 0
    for sent < length_of(buffer) ->
        count, error := net_send(connection, buffer[sent:])
        if error not= .NONE ->
            return error
        if count == 0 ->
            return .OTHER
        sent += count
    return .NONE

/// Closes `connection`, which gives its handle back to the system, after the bytes that it sent
/// were told to the other end as the end of what it sends. A socket is closed once, and the one
/// that was given is not usable after: `Net_Error.SOCKET_INVALID` is the answer for a socket that
/// is not open, such as one that `net_accept` could not give.
net_close :: proc(connection: Socket) Net_Error ->
    if connection.handle == INVALID_SOCKET_VALUE ->
        return .SOCKET_INVALID
    // A socket that listens has nothing to shut down, and that is not an error here.
    shutdown(connection.handle, SD_SEND)
    if closesocket(connection.handle) not= 0 ->
        return net_error_from(WSAGetLastError())
    return .NONE
