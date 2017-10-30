## 创建host

文件：`host.c`

```c
ENetHost * enet_host_create (const ENetAddress * address, size_t peerCount, size_t channelLimit, enet_uint32 incomingBandwidth, enet_uint32 outgoingBandwidth)
```
> __remark__：ENet会策略性的在连接中丢掉一些包以确保发送网络包时不会超过主机的带宽。参数中的bandwith同样确定了窗口的大小以限制在传输过程中的可靠包的数量。

在创建host的过程中主要进行了host和host->peers的初始化工作。


