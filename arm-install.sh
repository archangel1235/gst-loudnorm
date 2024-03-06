
#!/bin/bash

# copy shared libraries to /usr/lib

sudo cp -r binaries/arm64/lib/* /usr/lib

# copy header files to /usr/include create folder if not exist

sudo mkdir -p /usr/include

sudo cp -r binaries/arm64/include/* /usr/include

# remove old symbolic links
sudo rm /usr/lib/libebur128.so
sudo rm /usr/lib/libebur128.so.1

# make required symbolic links

sudo ln -s /usr/lib/libebur128.so.1.2.4 /usr/lib/libebur128.so.1
sudo ln -s /usr/lib/libebur128.so.1 /usr/lib/libebur128.so

echo "Done"