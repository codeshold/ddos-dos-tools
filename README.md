## DDos/DoS工具集

本仓库包含了各类DDoS/DoS工具，包括开源的工具和自己编写脚本。
相应工具自己已使用在实际的DDoS/DoS攻击测试中...

### SynFlood 攻击
> 借助`netsniff-ng`套件中的`trafgen`工具，其可伪造源ip发起DDoS攻击

1. [synflood.trafgen](https://github.com/wenfengshi/ddos-dos-tools/blob/master/synflood.trafgen)是对应的配置文件模版，修改文件里的源／目的MAC地址以及源／目的IP后，命令行直接运行`rafgen –cpp –dev eth0 –conf syn.trafgen –cpu 2 –verbose｀即可发起synflood攻击
2. `trafgen`是一款快速的，多线程发包工具，官方测试显示其发包速度可达到12Mpps，自己在`Intel(R) Xeon(R) CPU E5-2620 v3 @ 2.40GHz`下测得的发包速率有500Mbit/s多。通过对比其他开源程序，本工具的发包性能是本人测试中最高的

### SSL 攻击
1. [thc-ssl-dos](https://github.com/wenfengshi/ddos-dos-tools/tree/master/thc-ssl-dos)是一款开源的ssl攻击程序，使用的ssl重新协商机制，但对于关闭了或不支持重协商的服务端，该工具将失效。
2. [ssl-dos.sh](https://github.com/wenfengshi/ddos-dos-tools/blob/master/ssl-dos.sh)是写的一个简单的ssl攻击工具，同时适用于不支持ssl重协商的服务端，该脚本借助的是openssl工具。



