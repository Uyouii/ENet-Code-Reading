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

设置command发送给已连接的peer。

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
调用`enet_peer_queue_outgoing_command`将command添加到peer的command处理队列中。


## 检测peer连接超时或者丢包

函数：
```c
static int
enet_protocol_check_timeouts (ENetHost * host, ENetPeer * peer, ENetEvent * event)
```

位置：`protocol.c`

遍历该peer的`sentReliableCommands`，如果发现该command已经发送的时间超过`roundTripTimeout`，则将该命令的`roundTripTimeout`增加一倍，并将该command插入到发送队列重新发送。

如果该command的`roundTripTimeout`超过`roundTripTimeoutLimit`并且该command的已经发送的时间超过认为丢包的最小的时间限制，则认为该peer已经断开连接，则调用
```c
//一个peer断开连接后需要重新计算带宽
enet_protocol_notify_disconnect (host, peer, event);
```
并将event设定为断开连接事件，并返回1。
在`enet_protocol_notify_disconnect`中将`host -> recalculateBandwidthLimits`置为1，即当一个peer断开连接后需要重新计算和分配host的带宽。


## 发送reliable outging command
函数：
```c
static int
enet_protocol_send_reliable_outgoing_commands (ENetHost * host, ENetPeer * peer)
```

文件位置：`protocol.c`

向peer发送`peer->outgoingReliableCommands`中的command。

如果发送的数据没有超过发送窗口的大小的限制或者host的commands和buffer没有超过限制，则将该command从`outgoingReliableCommands`队列转移到`sentReliableCommands`队列，并设置该command的`roundTripTimeout`，标记该command占用的channel的usedReliableWindows，并将要发送的command和command中的packet存放到host的buffer中。

如果peer中存在可以发送的command，则返回1，否则返回0。

## 发送unreliable outgoing command

函数：
```c
static void
enet_protocol_send_unreliable_outgoing_commands (ENetHost * host, ENetPeer * peer)
```

文件位置：`protocol.c`

向peer发送`peer -> outgoingUnreliableCommands`中的command。

如果发送的数据没有超过发送窗口的大小的限制或者host的commands和buffer没有超过限制，则将该command从放置到host的command和buffer中。如果该command带有packet，则将该command转移到sent队列中，否则直接释放该command。

>❓ 发送前的一些排除操作不是很理解。

## 发送outgoing command

函数：
```c
static int
enet_protocol_send_outgoing_commands (ENetHost * host, ENetEvent * event, int checkForTimeouts)
```

文件位置：`procotol.c`

循环将与host相连的所有peer中的outgoing command发送出去，包括reliable command和unreliable command。

首先调用`enet_protocol_send_acknowledgements (host, currentPeer)`处理确认队列(`currentPeer -> acknowledgements`)，如果是断开连接的请求则需要特殊处理

随后调用`enet_protocol_check_timeouts (host, currentPeer, event)`检测是否存在连接超时或者丢包的情况，如果丢包则重发该包，如果连接超时则返回1。

随后调用`enet_protocol_send_reliable_outgoing_commands (host, currentPeer)`将尝试将reliable outging command转移到sent中,如果转移之后sent队列还是空的，并且距离上次ping 该peer的时间间隔大于时间间隔限制，则调用`enet_peer_ping (currentPeer)`将ping的command放在outgoing
 command队列中，再次调用`enet_protocol_send_reliable_outgoing_commands`将outgoing command中的command放在sent队列和host的buffer中。

 调用`enet_protocol_send_unreliable_outgoing_commands (host, currentPeer)`发送unreliable commmand

 随后在host的buffer[0]的位置设置发送的header和校验和，并检测要发送的buffer中的数据是否有压缩的空间，如果有压缩的空间则进行压缩后再发送。随后将本次循环中hostbuffer中的数据发送出去，更新host的totalsentdata和totalsentpackets。




<br>
<br>
<br>



> ❓ 问题：<br>
> 2. command中incomingBandwidth和outgoingBandwidth的含义？<br>
> 3. peer和host怎么处理queue中的command的<br>
> 4. peer -> packetThrottleCounter<br>
> 5. host中的buffer和command的作用<br>
> 6. 压缩算法是怎么压缩的<br>