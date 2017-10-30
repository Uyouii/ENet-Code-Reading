## 创建host

所在源文件：`host.c`

函数：
```c
ENetHost * enet_host_create (const ENetAddress * address, size_t peerCount, size_t channelLimit, 
enet_uint32 incomingBandwidth, enet_uint32 outgoingBandwidth)
```
> **remark**：ENet会策略性的在连接中丢掉一些包以确保发送网络包时不会超过主机的带宽。参数中的bandwith同样确定了窗口的大小以限制在传输过程中的可靠包的数量。

在创建host的过程中主要进行了host和host->peers的初始化工作。

## 检测host事件

所在源文件：`protocol.c`

函数：
```c
static int
enet_protocol_dispatch_incoming_commands (ENetHost * host, ENetEvent * event)
```
检查该host有无需要处理的事件，如果不存在需要处理的事件则返回0，否则处理后返回1

从host->dispatchQueue中弹出一个peer，如果peer状态为**等待**(`ENET_PEER_STATE_CONNECTION_PENDING`)或者**连接成功**(`ENET_PEER_STATE_CONNECTION_SUCCEEDED`)，则将其状态改为已经连接，事件类型定义为**连接**(`ENET_EVENT_TYPE_CONNECT`)，并返回1

如果peer状态为**无响应**(`ENET_PEER_STATE_ZOMBIE`)，则将事件类型定义为**断开连接**(`ENET_EVENT_TYPE_DISCONNECT`)，并将peer重置

如果peer状态为**已连接**(`ENET_PEER_STATE_CONNECTED`)，则检测该peer有无发送的packet，如果无发送的packet，则进行下一个循环

如果有发送的packet，则从peer的packcet的队列中弹出一个packet，放在event->packet中，并将事件类型定义为**接收**(`ENET_EVENT_TYPE_RECEIVE`)。如果仍有需要发送的packet，则将peer压入host事件处理队列的尾部

如果是其他状态，则继续检测下一个peer的状态时候符合上述条件。