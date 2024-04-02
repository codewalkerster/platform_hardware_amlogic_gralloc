#!/usr/bin/env bash

#当前存在的问题：
# 1. hardware/amlogic/gralloc/amlogic 目录被外部模块引用，没办法放到版本文件夹下。
#    所以该目录下引用 src 目录下的文件时，须修改其关联的文件夹位置
# 2. 外部模块引用了 am_gralloc_uvm_ext.h，而am_gralloc_uvm_ext.h又 include src/core 下的文件
#    相当于src/core下面的文件被外部模块间接引用，所以如果将 src 放到版本号文件夹中，编外部模块时
#    会报找不到需要的 src/core 目录

version=$1

command pushd "$(dirname "$0")" >/dev/null

function func_enable {
    mv "${1}" "${1%.disabled}"
}
function func_disable {
    mv "${1%}" "${1%}.disabled"
}

# for the last version
if [ $version = "r47p0" ]; then
    GRALLOC_FILES=$(find ./ \( -path ./amlogic -prune -o -path ./r44p1 -prune \) -o -name Android.bp.disabled -print)
    for FILE in $GRALLOC_FILES; do
        func_enable "${FILE}"
    done

    GRALLOC_FILES=$(find ./r44p1 -name Android.bp -print)
    for FILE in $GRALLOC_FILES; do
        func_disable "${FILE}"
    done

    # do some special change
    sed -i "s/hardware\/amlogic\/gralloc\/r..p./hardware\/amlogic\/gralloc/g" amlogic/Android.bp
    sed -i 's/libgralloc_headers/arm_gralloc_headers/g' amlogic/Android.bp
else
    GRALLOC_FILES=$(find ./ \( -path ./amlogic -prune -o -path ./${version} -prune \) -o -name Android.bp -print)
    for FILE in $GRALLOC_FILES; do
        func_disable "${FILE}"
    done

    GRALLOC_FILES=$(find ./${version} -name Android.bp.disabled -print)
    for FILE in $GRALLOC_FILES; do
        func_enable "${FILE}"
    done

    sed -i "s/hardware\/amlogic\/gralloc\/r..p./hardware\/amlogic\/gralloc/g" amlogic/Android.bp
    sed -i "s/hardware\/amlogic\/gralloc/hardware\/amlogic\/gralloc\/${version}/g" amlogic/Android.bp
    sed -i 's/arm_gralloc_headers/libgralloc_headers/g' amlogic/Android.bp
fi

command popd > /dev/null