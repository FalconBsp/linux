Expectations and Requirements
-----------------------------

- You can carry out these steps on the target system but be prepared for it
  to take significantly longer. For example, it may take 1 day to
  complete the build and installation process, if building on a GX-424
  2.4 GHz Merlin Falcon system with a single disk.
- Successful completion of this process requires good Linux and trouble
  shooting skills, including familiarity with system installation,
  packaging, building software from source, and kernel installation and
  configuration.
- A fast Internet connection is required to download source code and system
  updates.
  
 Script and necessary files
 --------------------------
 The instruction below referencethe following files
 - rbh.sh
 - xorg.conf
 - amdgpu.so.conf
 - environment
 - gstomx.conf
 - config-ubuntu14.04.2
 - SOURCES.tar.bz2
 - amd_trusted_apps
 - binaries


Follow these steps to install the latest open source amdgpu driver, with
UVD-accelerated video decoding and VCE-accelerated video encoding on Merlin Falcon.

1) Install Ubuntu 14.04.2 x86_64 with all available updates.

2) Install required packages.

$> sudo apt-get install git packaging-dev kernel-package yasm
$> sudo apt-get install libfreetype6-dev
$> sudo apt-get install libfribidi-dev
$> sudo apt-get install libfontconfig1-dev 
$> sudo apt-get install devscripts equivs
$> sudo apt-get install libwayland-dev
$> sudo apt-get install libxrandr*
$> sudo apt-get install faad libfaad-dev libfaac-dev alsa-oss libasound2-dev libalsa-* faac  
$> sudo apt-get build-dep alsa-base alsa-firmware-loaders alsa-oss alsa-utils

3) Configure git

	$> git config --global user.email "myname@email.com"
	$> git config --global user.name "FirstName LastName"

4) Extract sources

	$> tar -xvjf SOURCES.tar.bz2

5) Build and package the new linux kernel.

	$> cd SOURCES/kernel
	$> cp ../../configfiles/config-ubuntu14.04.2 .
	$> yes '' | make oldconfig
	$> make-kpkg --rootcmd=sudo --initrd --append-to-version=-1-amd \
	   kernel-image kernel-headers

   This will create linux-image-xxx.deb and linux-headers-xxx.deb files,
   where xxx is the version string of the generated packages.

6) Install the built linux kernel and header files.

	$> sudo dpkg -i linux_image-xxx.deb
	$> sudo dpkg -i linux_header-xxx.deb

        Add amdgpu.dpm=1 to kernel boot parameters

	You might need to generate the initramfs manually.

	$> sudo update-initramfs -c -k \
           3.10.67-ltsi.nl-amd-x86-64-1-amd
	$> sudo update-grub

7) Update amdgpu firmware.

	$> ./update-amdgpu-firmware.sh

	Also copy asd.bin and hdcp14tx_ta.bin files to
		/lib/firmware/amdgpu manually.

	$> sudo cp ./amd_trusted_apps/libamdsecurity/bin/hdcp14/asd.bin \
	   /lib/firmware/amdgpu
	$> sudo cp ./amd_trusted_apps/libamdsecurity/bin/hdcp14/hdcp14tx_ta.bin \
	   /lib/firmware/amdgpu

8) Reboot system to new kernel.

	Once rebooted, confirm that the amdgpu module is loaded and properly
	initialized:

	$> dmesg | grep -i uvd
	[    9.903069] [drm] UVD initialized successfully.
	$> dmesg | grep -i vce
	[   14.737978] [drm] VCE initialized successfully.

If you do not see the "UVD initialized successfully" or "VCE initialized
successfully" line, the amdgpu module has not been loaded and properly
initialized. Investigate and resolve this before going any further.

9) Build amdgpu driver user space. 

Edit .bashrc file in the user's home directory, and place the below
contents at the end of the file.

export RBH_PREFIX=/usr/local/xorg
export LD_LIBRARY_PATH=/usr/local/lib:$RBH_PREFIX/lib:$RBH_PREFIX/lib/vdpau:$LD_LIBRARY_PATH
export CPATH=/usr/local/include:$RBH_PREFIX/include:$CPATH
export PKG_CONFIG_PATH=$RBH_PREFIX/share/pkgconfig:/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
export PATH=/usr/local/bin:$PATH

Source the just modified .bashrc file.

	$> source ~/.bashrc

Run the rbh.sh script to download, configure, build and install the user
space components. They will be installed to /usr/local/xorg so as not to
disturb any installed packages.

Building user space needs internel connection. So, please reboot to a kernel
having internet connection.

	$> ./rbh.sh --buildall

Note: Building 'mesa' has dependency on libvdpau and libva, so
      the build will throw an error. At this stage, you need to
      first install libvdpau and libva and then continue with
      the build.

	$> cd SOURCES/libvdpau-1.1.1
	$> make clean
	$> sudo apt-get install libepoxy-dev
	$> ./autogen.sh
	$> make
	$> sudo make install

	$> cd SOURCES/libva
	$> make clean
	$> ./autogen.sh
	$> make
	$> sudo make install

	$> ./rbh.sh --buildall

Note: You need to use the patched version of libva which is
      provided as part of this release package.

10) Build gtsreamer, gst-plugins-base, gst-plugins-good, gst-plugins-bad,
    gst-plugins-ugly and gstreamer-vaapi from sources. Ex.

	$> cd SOURCES/gstreamer-1.x
	$> sudo apt-get install libglib2.0-dev
	$> make clean
	$> autoreconf -f -i
	$> ./configure
	$> make
	$> sudo make install
  
    Please install remaining plugins as written in order.

11) Install the provided xorg.conf to /etc/X11/xorg.conf

	$> sudo cp configfiles/xorg.conf /etc/X11/xorg.conf
	
	$> sudo chattr +i /etc/X11/xorg.conf

Make sure the BusID of the Device section matches your target system.
You can use lspci to confirm.

12) Make sure libglx uses the new DRI drivers

Unfortunately, the DRI driver path is hard coded in libglx so it's
neccessary to move aside the existing DRI driver directory and replace it
with a symbolic link to the drivers just built.

	$> sudo mv /usr/lib/x86_64-linux-gnu/dri{,~}
	$> sudo ln -s /usr/local/xorg/lib/dri /usr/lib/x86_64-linux-gnu/dri

	$> sudo mv /usr/lib/x86_64-linux-gnu/vdpau{,~}
	$> sudo ln -s /usr/local/xorg/lib/vdpau /usr/lib/x86_64-linux-gnu/vdpau

13) Make sure your user is in the video group.

If not, add yourself:

	$> sudo usermod -a -G video $LOGNAME

14) Modify the dynamic loader configuration

Certain system components (X server, Compiz, etc.) must load the newly
built libraries (libGL, libEGL, etc.) instead of the system provided ones.

	$> sudo cp configfiles/amdgpu.so.conf /etc/ld.so.conf.d/
	$> sudo mv /etc/ld.so.conf.d/x86_64-linux-gnu_GL.conf{,~}
	$> sudo mv /etc/ld.so.conf.d/x86_64-linux-gnu_EGL.conf{,~}
	$> sudo ldconfig

15) Configure the system wide environment

It's necessary to specify the OpenGL driver path and VDPAU driver. Append
the provided environment file to the system-wide environment.
	
	$> cd configfiles
	$> sudo sh -c 'cat environment >> /etc/environment'

16) Add GStreamer OpenMAX configuration

	$> cp gstomx.conf ~/.config/gstomx.conf

17) Reboot. 

18) Register OpenMAX library files

	$> omxregister-bellagio -v

19) Confirm correct operation.

Check /var/log/Xorg.0.log to find any errors in loading amdgpu driver.

Run glxinfo to confirm OpenGL is correctly configured
	$> glxinfo

Run vdpauinfo to confirm VDPAU (UVD support) is currently configured
	$> vdpauinfo
	
Create a soft link to gallium_drv_video.so
	$> sudo ln -s /usr/local/xorg/lib/dri/gallium_drv_video.so /usr/local/lib/dri/radeonsi_drv_video.so

Run vainfo to confirm VA (UVD support) is currently configured
	$> vainfo


Install mpv with h265 playback support:
--------------------------------------
	
	$ cd mpv-build
	$ ./rebuild -j4
	$ sudo ./install 
	
	
OpenCL library support:
-----------------------
1) Copy OCL and hsa libs
	$ cd ./binaries/OCL-HSA/
	$ tar -jxvf OCL_HSA_LIBS.tar.bz2 
	$ cd opt/
	$ sudo mv hsa /opt/
	
2) Install AMD APP SDK from 
http://developer.amd.com/tools-and-sdks/opencl-zone/amd-accelerated-parallel-processing-app-sdk/ 

3) Go to APP SDK installed path
	$ cd lib/x86
	$ rm libamdocl* libOpenCL.so libOpenCL.so.1
	$ cd lib/x86_64
	$ rm libamdocl* libOpenCL.so libOpenCL.so.1
	$ cd lib/x86_64/sdk
	$ rm libamdocl64.so  libOpenCL.so  libOpenCL.so.1
	
4) edit ~/.bashrc
make sure below paths are added properly as mentioned below

export AMDAPPSDKROOT="/home/amd/AMDAPPSDK-3.0"
export LD_LIBRARY_PATH="/home/amd/AMDAPPSDK-3.0/lib/x86_64/"
export OPENCL_VENDOR_PATH="/home/amd/AMDAPPSDK-3.0/etc/OpenCL/vendors/"
export RBH_PREFIX=/usr/local/xorg
export LD_LIBRARY_PATH=/usr/local/lib:$RBH_PREFIX/lib:$RBH_PREFIX/lib/vdpau:/opt/hsa/lib:$LD_LIBRARY_PATH
export CPATH=/usr/local/include:$RBH_PREFIX/include:$CPATH
export PKG_CONFIG_PATH=$RBH_PREFIX/share/pkgconfig:/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH


5) source ~/.bashrc

6) echo "KERNEL==\"kfd\", MODE=\"0666\"" | sudo tee /etc/udev/rules.d/kfd.rules

7) reboot

8) clinfo - should display as AMD HSA Device.

9) Run AMD SDK samples.


TEE feature verification steps
------------------------------

1) Goto amd_trusted_apps/libamdsecurity/mobicore/clientlib

	$ make && sudo make install

2) Copy binaries

	$> sudo cp ./amd_trusted_apps/libamdsecurity/bin/tee/libMcClient.so /usr/lib
	$> sudo mkdir /data
	$> sudo cp ./amd_trusted_apps/libamdsecurity/bin/tee/*.tlbin /data
	$> sudo cp ./amd_trusted_apps/libamdsecurity/bin/tee/mcTest /data

3) Run mcTest

	$> su root
	$> cd /data
	$> ./mcTest

GlobalPlatform Test:
-------------------
	$> sudo cp libamdsecurity/bin/tee/GP/caSampleRot13 /data
	$> sudo cp libamdsecurity/bin/tee/GP/d51a83c9474a5655af2a58dcfd2b1d37.tabin /data
	$> sudo –i <Become root user>
	$> cd /data
	$> ./caSampleRot13


Secure Playback verification:
----------------------------

1) Please refer README at amd_trusted_apps/libamdsecurity/bin/tools/encrypt_content


