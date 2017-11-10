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

在有新的peer连接或者peer断开连接时，则需要重新计算带宽限制。

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

遍历该peer的`sentReliableCommands`，如果发现该command已经发送的时间超过`roundTripTimeout`，则将该命令的`roundTripTimeout`增加一倍，并将相应的command从sent队列重新转移到outgoing comand队列的头部。

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

文件：`protocol.c`

向peer发送`peer->outgoingReliableCommands`中的command。

如果发送的数据没有超过发送窗口的大小的限制或者host的commands和buffer没有超过限制，则将该command从`outgoingReliableCommands`队列转移到`sentReliableCommands`队列，并设置该command的`roundTripTimeout`，标记该command占用的channel的usedReliableWindows，并将要发送的command和command中的packet存放到host的buffer中。

如果peer中存在可以发送的command，则返回1，否则返回0。

## 发送unreliable outgoing command

函数：
```c
static void
enet_protocol_send_unreliable_outgoing_commands (ENetHost * host, ENetPeer * peer)
```

文件：`protocol.c`

向peer发送`peer -> outgoingUnreliableCommands`中的command。

如果发送的数据没有超过发送窗口的大小的限制或者host的commands和buffer没有超过限制，则将该command从放置到host的command和buffer中。如果该command带有packet，则将该command转移到sent队列中，否则直接释放该command。

在发送前会根据`peer->packetthrottle`的大小概率性的丢掉一些unreliable command。如果`packetThrottle`等于`ENET_PEER_PACKET_THROTTLE_SCALE`，则一定不会丢掉。
如果`packetThrottle`大小为0，则将unrelibale的command全部丢掉。

## 发送outgoing command

函数：
```c
static int
enet_protocol_send_outgoing_commands (ENetHost * host, ENetEvent * event, int checkForTimeouts)
```

文件：`procotol.c`

循环将与host相连的所有peer中的outgoing command发送出去，包括reliable command和unreliable command。

首先调用`enet_protocol_send_acknowledgements (host, currentPeer)`处理确认队列(`currentPeer -> acknowledgements`)，如果是断开连接的请求则需要特殊处理

随后调用`enet_protocol_check_timeouts (host, currentPeer, event)`检测是否存在连接超时或者丢包的情况，如果丢包则重发该包，如果连接超时则返回1。

随后调用[`enet_protocol_send_reliable_outgoing_commands (host, currentPeer)`]()将尝试将reliable outging command转移到sent中,如果转移之后sent队列还是空的，并且距离上次ping 该peer的时间间隔大于时间间隔限制，则调用`enet_peer_ping (currentPeer)`将ping的command放在outgoing
 command队列中，再次调用`enet_protocol_send_reliable_outgoing_commands`将outgoing command中的command放在sent队列和host的buffer中。

 调用`enet_protocol_send_unreliable_outgoing_commands (host, currentPeer)`发送unreliable commmand

 随后在host的buffer[0]的位置设置发送的header和校验和，并检测要发送的buffer中的数据是否有压缩的空间，如果有压缩的空间则进行压缩后再发送。随后将本次循环中hostbuffer中的数据发送出去，更新host的totalsentdata和totalsentpackets。



## 处理incoming command

函数：
```c
static int
enet_protocol_handle_incoming_commands (ENetHost * host, ENetEvent * event)
```

文件：`protocol.c`

首先验证peer是否已经断开连接或者准备断开连接，或者地址和peerID信息是否匹配。如果断开连接或者信息不匹配这返回0。

如果收到的数据之前被压缩过，则进行解压缩操作。

遍历`receiveData`中的每个命令根据其commandNumber进行分类处理。

如果该command是需要确认的请求(`ENET_PROTOCOL_COMMAND_FLAG_ACKNOWLEDGE`)并且该peer的状态没有断开，则将该command放在host的`acknowledge`中。

## 确认接收realiable command

函数：
```c
static ENetProtocolCommand
enet_protocol_remove_sent_reliable_command (ENetPeer * peer, enet_uint16 reliableSequenceNumber, enet_uint8 channelID)
```

文件：`protocol.c`

从sentReliableCommands队列中确认之前发送的command。

首先遍历该peer的`sentReliableCommands`，如果没有找到相应的command，则遍历该peer的`outgoingReliableCommands`(有可能之前发送该command超时后将该command从sent队列转移回了outgoing队列。

如果没有找到该command则返回`ENET_PROTOCOL_COMMAND_NONE`。

如果找到该command，则将其占用的channel和window释放出来，并从相应的队列中移除这个command，如果该command带有packet，则释放相应的packet。

并为该peer的sent队列头的packet设置下次的超时时间。

返回相应的command number。

## 根据roundtriptime调节peer throttle

函数：
```c
int
enet_peer_throttle (ENetPeer * peer, enet_uint32 rtt)
```

文件：`peer.c`

rtt为这次packet的roundtriptime，packet throttle是用来调节发送时buffer的windowSize的。

如果上次的传输时间非常短
```c
peer -> lastRoundTripTime <= peer -> lastRoundTripTimeVariance
```
则将`peer -> packetThrottle`调节到最大值(`peer -> packetThrottleLimit`)。

如果这次的传输速度比上次快，
```c
rtt < peer -> lastRoundTripTime
```
则将`peer->packetThrottle`增加响应的值(`peer -> packetThrottleAcceleration`)

如果这次的传输时间比较慢
```c
rtt > peer -> lastRoundTripTime + 2 * peer -> lastRoundTripTimeVariance
```
则将`peer -> packetThrottle`减少响应的值(`peer -> packetThrottleDeceleration`)


## 处理acknowledge

函数：
```c
static int
enet_protocol_handle_acknowledge (ENetHost * host, ENetEvent * event, ENetPeer * peer, const ENetProtocol * command)
```

处理连接确认的请求。

如果peer的状态时断开连接或者准备断开连接，或者command的时间超过限制则不处理。

根据该command的roundtriptime调节peer的`packetThrottle`，`roundTripTime`和`roundTripTimeVariance`。

如果之前进行过流量控制或者流量控制的时间间隔超过一定的时间(`peer -> packetThrottleInterval`)，则进行peer的roundtirptime相关设置。

从peer的`sentReliableCommands`队列中根据`receivedReliableSequenceNumber`移除该command。

如果时确认连接或者确认断开连接的command，则进行相应的peer添加和删除操作。

## queue incoming command

函数：
```c
ENetIncomingCommand *
enet_peer_queue_incoming_command (ENetPeer * peer, const ENetProtocol * command, const void * data, size_t dataLength, enet_uint32 flags, enet_uint32 fragmentCount)
```

文件：`peer.c`

如果是fragement的命令，无论是reliable command还是unreliable command，传入的只是这些fragements的start command。

如果是reliable command，如果有重复的就丢掉。如果其所在window范围不超过滑动窗口的传输范围，则在channel的`incomingReliableCommands`队列中找到合适的位置将其插入。

❓ 有个排除操作没有看懂。

unrelibale 都会携带一个reliable sequence number，用来标记该unreliable command相对于relibale command的位置。

如果该unreliable前的reliable command已经到达，并且如果序号较大的unrelibale command已经到了，则舍弃该unreliable command，如果序号重复的command，也舍弃掉。
否则在`incomingUnreliableCommands`中找到合适的位置将该command插入。

如果是fragements的strat command，则为其创建一个fragements的成员变量，用位图的方法记录其fragements的到达的情况。

随后将incoming commands dispatch到dispatched command中。

## 处理fragement

函数：
```c
static int
enet_protocol_handle_send_fragment (ENetHost * host, ENetPeer * peer, const ENetProtocol * command, enet_uint8 ** currentData)
```

文件：`protocol.c`

首先根据其stratcommand number在incomingReliableCommands中寻找其startcommand。如果找到的话则将其返回，否则将该command的sequence number设置为start command，并调用`enet_peer_queue_incoming_command`将其插入到`incomingReliableCommands`中。

随后将属于fragements中的数据集中到start command中。等到没有fragement剩余后，调用`enet_peer_dispatch_incoming_reliable_commands`将其dispatch到dispatched command中。

## 处理unreliable fragement

函数：
```c
static int
enet_protocol_handle_send_unreliable_fragment (ENetHost * host, ENetPeer * peer, const ENetProtocol * command, enet_uint8 ** currentData)
```

文件：`protocol.c`

过程与`enet_protocol_handle_send_fragment`类似，会额外排除掉一些可以丢弃的unreliable command。













<br>
<br>
<br>



❓ 问题：

> 2. command中incomingBandwidth和outgoingBandwidth的含义？

> 3. peer和host怎么处理queue中的command的 ？

host发送命令时首先会把命令放到outgoingcommands队列中，每次发送时会将outgoingcommands队列中的各个命令取出放到buffer中，直到buffer放满或者command没有剩余，随后将buffer中的数据压缩后发送给peer(发送调用的时操作系统的socket接口)。对于需要ack的command，会将该command存到sentcommand队列中，当收到ack时会用sequence number和set队列中的command进行对比，将sent队列中的相应的command删除。host会定期检查sent队列中的command有没有超时，如果超时先检测peer有没有断开，如果peer已经断开，则重置peer，如果没有断开，则重发该command，将该command从sent队列取出放回到outgoing command中。

每当host收到一个command时，会将该command放到host的incomingcommands队列中，对于reliable command，则等待相应顺序的command都到达后将该command移动到dispatched commands队列中。当检测到host的dispatchQueue没有被占用后，将该peer push到host的dispatchQueue中，随后在host service中处理dispatchQueue中相应的内容。

> 4. peer -> packetThrottleCounter

根据throttle的大小按概率直接丢掉一些unreliable command

> 5. host中的buffer和command的作用
buffer是用来发送数据的储存空间，每次host将buffer存满后就将buffer中的数据压缩后发送到该peer。

> 6. 压缩算法是怎么压缩的


> 7. packet的reference count是干什么用的？

有时一个要发送的packet的数据量比较大，会将该packet分到多个command中发送出去，每个command会将该packet的reference count增加1，每处理一个command会将packet的reference count减一，当一个packet的reference count到0时则将该packet释放掉。
