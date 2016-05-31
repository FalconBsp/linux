#/bin/sh

BASE_KERNEL=3.10.67
DATE=`date +%Y%m%d`
SCEL_VER=u.2.0.2.3
COMMENT=$1

if [ $1 ]
then
 fakeroot debian/rules clean
 echo "Current deb build package will be " $BASE_KERNEL.$DATE-$SCEL_VER
 export DEBEMAIL="sony@sony.com"
 export DEBFULLNAME="SCEL developer"
 dch -v $BASE_KERNEL.$DATE-$SCEL_VER -b $COMMENT
else
 echo "Please add your comment"
fi

