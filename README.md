## DDos/DoS工具集

本仓库包含了多类DDoS/DoS工具，且相应工具自己已使用到实际的DDoS/DoS攻击测试中...

### 1. SynFlood 攻击
> 借助`netsniff-ng`套件中的`trafgen`工具，其可伪造源ip发起DDoS攻击

- `trafgen`是一款高速的，多线程数据包生成器，官方测试显示其速度可达到12Mpps，自己在`Intel(R) Xeon(R) CPU E5-2620 v3 @ 2.40GHz`下测得的发包速率有500Mbit/s多。通过对比其他开源程序，本工具的发包性能是自己测试中性能表现最高的。
- [synflood.trafgen](https://github.com/wenfengshi/ddos-dos-tools/blob/master/synflood.trafgen)是对应的配置文件模版，修改文件里的源／目的MAC地址以及源／目的IP后，命令行直接运行`trafgen –cpp –dev eth0 –conf syn.trafgen –cpu 2 –verbose｀即可发起synflood攻击
- 对应工具可直接通过在线源进行安装，CentOS下`yum install netsniff-ng`即可安装整个套件，其中包含trafgen等工具。（预先需可能需安装fedora源，`yum install epel-release.noarch -y`



### 2. SSL 攻击
- [thc-ssl-dos](https://github.com/wenfengshi/ddos-dos-tools/tree/master/thc-ssl-dos)是一款有名的ssl攻击程序，原理是ssl重新协商机制，但对于关闭了的或不支持SSL重协商的服务端，该工具将失效。
- [ssl-dos.sh](https://github.com/wenfengshi/ddos-dos-tools/blob/master/ssl-dos.sh)是自己写的一个简单的ssl攻击工具，且适用于不支持ssl重协商的服务端，该脚本借助的是openssl工具。



