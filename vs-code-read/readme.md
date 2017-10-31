# 函数解析

## 创建host

函数：
```c
ENetHost * enet_host_create (const ENetAddress * address, size_t peerCount, size_t channelLimit, 
enet_uint32 incomingBandwidth, enet_uint32 outgoingBandwidth)
```
所在源文件：`host.c`

> **remark**：ENet会策略性的在连接中丢掉一些包以确保发送网络包时不会超过主机的带宽。参数中的bandwith同样确定了窗口的大小以限制在传输过程中的可靠包的数量。

在创建host的过程中主要进行了host和host->peers的初始化工作。

## 检测host事件

函数：
```c
static int
enet_protocol_dispatch_incoming_commands (ENetHost * host, ENetEvent * event)
```

所在源文件：`protocol.c`


检查该host有无需要处理的事件，如果不存在需要处理的事件则返回0，否则处理后返回1

从host->dispatchQueue中弹出一个peer，如果peer状态为**等待**(`ENET_PEER_STATE_CONNECTION_PENDING`)或者**连接成功**(`ENET_PEER_STATE_CONNECTION_SUCCEEDED`)，则将其状态改为已经连接，事件类型定义为**连接**(`ENET_EVENT_TYPE_CONNECT`)，并返回1

如果peer状态为**无响应**(`ENET_PEER_STATE_ZOMBIE`)，则将事件类型定义为**断开连接**(`ENET_EVENT_TYPE_DISCONNECT`)，并将peer重置

如果peer状态为**已连接**(`ENET_PEER_STATE_CONNECTED`)，则检测该peer有无发送的packet，如果无发送的packet，则进行下一个循环

如果有发送的packet，则从peer的packcet的队列中弹出一个packet，放在event->packet中，并将事件类型定义为**接收**(`ENET_EVENT_TYPE_RECEIVE`)。如果仍有需要发送的packet，则将peer压入host事件处理队列的尾部

如果是其他状态，则继续检测下一个peer的状态时候符合上述条件。


## 调节bandwidth throttle

函数：
```c
void
enet_host_bandwidth_throttle (ENetHost * host)
```
所在源文件：`host.c`


如果距离上次带宽控制的时间(`elapsedTime`)超过1秒，则重新进行带宽控制。

### 调节throttle

首先计算host在全带宽时在间隔时间内可以传输的数据量(`bandwidth`)
```c
bandwidth = (host -> outgoingBandwidth * elapsedTime) / 1000; 
```
随后统计与host相连的所有peer在间隔时间内发送的数据总量(`dataTotal`)
```c
for (peer = host -> peers;
    peer < & host -> peers [host -> peerCount]; ++ peer)
{
    if (peer -> state != ENET_PEER_STATE_CONNECTED && peer -> state != ENET_PEER_STATE_DISCONNECT_LATER)
        continue;
    dataTotal += peer -> outgoingDataTotal;
}
```


计算throttle
```c
//throttle = SCALE * bandwidth / (peer)dataTotal，最大是32
if (dataTotal <= bandwidth)
    throttle = ENET_PEER_PACKET_THROTTLE_SCALE;
else
    throttle = (bandwidth * ENET_PEER_PACKET_THROTTLE_SCALE) / dataTotal;
```
这里计算throttle用的是host的outgoing bandwidth和peer的outgoing data Total，可以理解为 peer的outgoing data大部分要通过host传输，可以类似等价为host这段时间内需要发送的数据量，host的outgoing bandwidth是host全带宽发送数据的能力，throttle可以理解为所有peer的发送数据能力与发送数据量的商的平均水平。

调节peer -> packetThrottleLimit 和 peer -> packetThrottle
```c
if ((peer -> state != ENET_PEER_STATE_CONNECTED && 
    peer -> state != ENET_PEER_STATE_DISCONNECT_LATER) ||	//peer是连接状态
    peer -> incomingBandwidth == 0 ||			        //允许接收数据
    peer -> outgoingBandwidthThrottleEpoch == timeCurrent)	//不是刚刚调节过
    continue;

peerBandwidth = (peer -> incomingBandwidth * elapsedTime) / 1000;	//peer在间隔时间内能接收的最大数据

//如果 （in coming)peerBandwith / peer->outgoingDataTotal >= bandwith / (peer)dataTotal
//如果此peer的接收数据的能力/发送数据量 大于平均水平时，则暂时不调节
if ((throttle * peer -> outgoingDataTotal) / ENET_PEER_PACKET_THROTTLE_SCALE <= peerBandwidth)
    continue;

//如果低于平均水平，则将peer->packetThrottleLimit重新计算
//否则则按照throttle计算
peer -> packetThrottleLimit = (peerBandwidth * 
                                ENET_PEER_PACKET_THROTTLE_SCALE) / peer -> outgoingDataTotal;

//packThrottleLimit最小值为1
if (peer -> packetThrottleLimit == 0)
    peer -> packetThrottleLimit = 1;

//设置packetThrottle
if (peer -> packetThrottle > peer -> packetThrottleLimit)
    peer -> packetThrottle = peer -> packetThrottleLimit;
```

完成上述步骤后将peer中剩下的还未处理过的(大于平均水平的)`peer->packThrottleLimit`设置为`throttle`

### 重新计算带宽限制

bandwidth为host的`host -> incomingBandwidth`,即host接收数据的能力
带宽限制取其平均数
```c
bandwidthLimit = bandwidth / peersRemaining;
```
首先将outgoing小于host接收数据能力平均水平的peer标记出来(用时间标记)
```c
if (peer -> outgoingBandwidth > 0 &&
peer -> outgoingBandwidth >= bandwidthLimit)
    continue;
//将outgoingBandwidth小于平均水平的peer标记出来
peer -> incomingBandwidthThrottleEpoch = timeCurrent;
```

为每个已连接的peer设置command。

将`outgoingBandwidth`设置为`host -> outgoingBandwidth`
如果之前标记过的，即`peer -> outgoingBandwidth`小于bandwidthLimit的peer将`incomingBandwidth`设置为`peer->outgoingBandwith`
否则将`incomingBandwidth`设置为`bandwidthLimit`

```c
command.bandwidthLimit.outgoingBandwidth = ENET_HOST_TO_NET_32 (host -> outgoingBandwidth);
//如果之前标记过，即小于平均水平的peer
if (peer -> incomingBandwidthThrottleEpoch == timeCurrent)
    command.bandwidthLimit.incomingBandwidth = ENET_HOST_TO_NET_32 (peer -> outgoingBandwidth);
else
    //没处理过的统一设置为 bandwidthlimit
    command.bandwidthLimit.incomingBandwidth = ENET_HOST_TO_NET_32 (bandwidthLimit);

//给peer设置一个command
enet_peer_queue_outgoing_command (peer, & command, NULL, 0, 0);
```
