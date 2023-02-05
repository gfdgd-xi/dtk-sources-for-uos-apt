受此仓库启发：https://gitee.com/deepin-community-store/spark-dtk/  
编译顺序：  
```bash
libdtkcommon---dtkcore---dtkgui----dtkwidget
```
编译方法：
```bash
fakeroot dpkg-buildpackage
```
提示哪里缺依赖就补哪里  