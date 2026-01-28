sudo ip netns add second
sudo ip netns add third
sudo ip link set enp101s0f2  netns second
sudo ip link set enp101s0f3  netns third
sudo ip netns exec second ip a a 192.168.1.11/24 dev enp101s0f2
sudo ip netns exec third  ip a a 192.168.1.12/24 dev enp101s0f3
sudo ip netns exec second ip link set  enp101s0f2 up
sudo ip netns exec third  ip link set  enp101s0f3 up
#sudo ip netns exec second bash
#su user
#sudo ip netns exec third bash
#su user
