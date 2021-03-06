### Simple Web Game Server

This project provides generic C++ classes to create multi-threaded game servers
and matchmaking servers for browser games using WebSockets and JWT
authentication. It is built
upon the [WebSocket++](https://github.com/zaphoyd/websocketpp) and
[JWT++](https://github.com/Thalhammer/jwt-cpp) libraries.
[Doxygen](https://www.doxygen.nl/index.html) documentation is available
[here](https://permutationlock.com/simple_web_game_server/).

#### Motivation

The core motivation for the libarary is to:

 - provide a simple framework to set up servers to run online multiplayer games
   and perform algorithmic matchmaking;
 - be as flexible as possible within this structure and allow for any
   game and any matchmaking algorithm;
 - be secure and authenticate clients in order to support competitive games;
 - have servers run independently with no need for external communication
   beyond messages to clients;
 - be performant and allow both horizontal and vertical scaling of backends.

With regards to the first two points, in order to create a backend for a game
all that needs to be written is a
specification for the JWTs you want to verify, a
class describing the game logic, and a class detailing the matchmaking
algorithm; see examples below.

Security is achieved by supporting TLS and requiring that a client's first message
when they connect contain a JWT verifying their identity and detailing their
intended session with the server.

With regards point four and server isolation, by default the servers in this
library listen only
for client connections and make no other external communication, e.g.
updating a databases.
However, each time a server session is completed, the server will send each
associated client a token verifying the result. Thus, if it is desired that server
activity be tracked, for example to track ranked matches in a
competitive game, each client may be designed to submit their result tokens to a
central location for tracking. Obviously, in order to guard against
malicious clients, there must be significant incentive for clients to
submit their results. However, this will usually exist
naturally, e.g. the winner in a competitive game will always wish to
submit the result.

Since servers run completely independently, it is easy to achieve horizontal
scaling by simply spinning up as many servers as desired and then pointing new
clients to them with the appropriate JWT authentication.

Vertical scaling is accomplished by allowing multi-threading in each
aspect of the server: multiple threads may be assigned to handle WebSocket
connections and messages, multiple threads may handle processing server
actions, and the game update loop may update the independent games in parallel.

**Note on latency and performance:** Currently most web
browser are restricted to only use the TCP
based WebSocket protocol for client-server socket communication,
[see this article for a nice explanation](https://gafferongames.com/post/why_cant_i_send_udp_packets_from_a_browser/).
Because of this restriction, the design of the server assumes that most games
using this library will not be twitch reaction
dependent real-time games and prioritizes performance over low latency.
Specifically, in order to
reduce mutex locking, all game updates are run on a fixed time-step, e.g. 16ms
or 60 updates per second; in which case we introduce the additional lag of
up to 16ms on client input. Of course, one could simply reduce this time-step
to zero and essentially eliminate all lag aside from the time it takes to
actually compute the updates, but understand that
this goes against the design in mind and could potentially harm performance.

#### Examples

 - [Minimal Template](https://github.com/permutationlock/simple_web_game_server/tree/main/examples/minimal_template):
   minimal examples of game and matchmaking servers, with and without TLS.
 - [Chat](https://github.com/permutationlock/simple_web_game_server/tree/main/examples/chat):
   a simple chat server displaying the basic features of the game server.
 - [Tic Tac Toe](https://github.com/permutationlock/simple_web_game_server/tree/main/examples/tic_tac_toe):
   a full implementation of a tic tac toe game with matchmaking,
   reconnection to games, and client submission of game and matchmaking results
   to a central server.

#### Basic idea of code structure

The base server class wraps a
[WebSocket++](https://github.com/zaphoyd/websocketpp) server and provides JWT
authentication as well as player and session identification for clients.
A player id is an identifier unique to each player while a session id is unique
to a particular session of interaction with the server.

When the first client with a given session id connects and provides a verified
JWT, a session is started. Each
session persists until it is ended by the server, at which point a result
token string is sent back to all participating clients.
Completed sessions are archived for a period of
time to allow clients to retrieve the result token; this allows
disconnected players to reconnect and see the result of a game for example.

In a game server a session represents a particular
game, with different players sharing the same session id if they are in the
same game. In a matchmaking server sessions have multiple potential uses
depending on the specification, e.g. allowing multiple players to queue for
a match together by sharing a session.
