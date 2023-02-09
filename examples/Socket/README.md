# Socket example

This example demonstrates the use of **Socket Event**.

**Socket Event** becomes signaled when one of the events specified when the object was created occurs on the bound socket. An analogue of such an object is **WSAEVENT** bound to a socket via **WSAEventSelect**. The restrictions described in **msdn** for **WSAEventSelect** also apply to it - It's impossible to bind more than one such event to a socket. When a second event is created with the same socket descriptor, the first event will stop to work.

This type of object is useful if, for example, there is a need to end a wait both when there is data to receive, and if a different type of event has occurred (for example, the user is stopping the service). A more complete socket implementation can be found in the **SocketPair**, **SSLSocket**, and **BIOSocket** classes. They have a built-in use of the socket event object.

This example then creates a server that opens a socket and waits for connection requests from clients. When a client appears, the server puts the socket into non-blocking mode and uses the **Socket Event** to wait for a request from the client. Upon receiving the request, the server sends a response to the client and exits. The client works with the socket in blocking mode and, after sending the request, waits for the server's response. After receiving a response from the server, the client exits.