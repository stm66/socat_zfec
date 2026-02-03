sudo apt install build-essential autoconf automake libtool zlib1g-dev
git clone https://github.com/openstack/liberasurecode.git
cd liberasurecode
./autogen.sh
./configure
make
sudo make install
sudo ldconfig
