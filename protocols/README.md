# 协议草案 v1（需与下游和上位机确认）

TCP 使用网络字节序。帧为 magic[2]=0x56,0x4d，version[1]=1，command[1]，sequence[4]，payload_length[4]，payload[0..4096]，crc32[4]。CRC覆盖帧头至payload末尾；CRC-32/ISO-HDLC，poly=0x04C11DB7，反射实现0xEDB88320，init/xorout=0xFFFFFFFF，refin/refout=true，校验向量123456789=CBF43926。最小16字节，最大4112字节。

已实现编解码、分片合并、错误CRC和非法长度重新同步；网络连接、命令执行、ACK/NACK尚未实现。候选命令：0x01心跳，0x10状态，0x20读参数，0x21写参数，0x7e ACK，0x7f NACK。ACK/NACK沿用请求序号，payload首字节为原命令，次字节为错误码；其余载荷模式需在实现前冻结。远程写参数默认禁用。重传缓存尚未实现，不承诺断网告警缓存。

CAN 草案：标准11-bit ID=0x700+node_id（1..127），DLC=8；byte0版本1，byte1节点ID，byte2..3序列号大端，byte4状态，byte5..7保留为0。ID与载荷节点号必须一致；心跳1秒，离线3秒，以单调时间检查。节点白名单初始1、2；拒绝RTR、扩展帧、错误长度和未知版本；CAN链路CRC由控制器处理。SocketCAN收发与节点管理待实现。
