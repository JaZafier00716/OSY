# hw6 – vícevláknový chat server

- `socket_srv.cpp` – server, jedno vlákno na klienta (`pthread_create` + `pthread_detach`).
- `socket_cl.cpp` – klient beze změny z `osy-prog-demo/socket-demo`.

```
make
./socket_srv 5000            # terminál 1 (text = zpráva "Server: ...", "quit" = konec)
./socket_cl localhost 5000   # terminál 2, 3, ... (nejdřív nick, "close" = odpojení)
```

Sdílený seznam `g_clients[]` (soket + nick) zatím není chráněn mutexem – synchronizace přijde v dalším úkolu.
