# Linux 常用命令速查

适用于 WSL / Ubuntu 环境。

---

## 目录

- [文件操作](#文件操作)
- [目录操作](#目录操作)
- [查看文件内容](#查看文件内容)
- [文件权限](#文件权限)
- [查找搜索](#查找搜索)
- [系统信息](#系统信息)
- [进程管理](#进程管理)
- [网络相关](#网络相关)
- [压缩归档](#压缩归档)
- [包管理 (apt)](#包管理-apt)
- [文本处理](#文本处理)
- [用户权限](#用户权限)
- [实用技巧](#实用技巧)

---

## 文件操作

```bash
# 列出文件
ls                    # 列出当前目录文件
ls -l                 # 详细信息（权限、大小、日期）
ls -la                # 含隐藏文件（以 . 开头的文件）
ls -lh                # 大小以易读格式显示（KB/MB）

# 创建文件
touch file.txt        # 创建空文件（或更新文件时间戳）

# 复制
cp file1.txt file2.txt          # 复制文件
cp -r dir1/ dir2/               # 递归复制目录
cp file.txt /path/to/dest/      # 复制到指定路径

# 移动 / 重命名
mv file.txt /new/path/          # 移动文件
mv oldname.txt newname.txt      # 重命名文件

# 删除
rm file.txt                     # 删除文件
rm -r dir/                      # 递归删除目录
rm -rf dir/                     # 强制删除（慎用！无回收站）

# 链接
ln -s /real/path link_name      # 创建软链接（快捷方式）
ln /real/path link_name         # 创建硬链接
```

---

## 目录操作

```bash
pwd                             # 查看当前路径

cd /home                        # 进入指定目录
cd ~                            # 回到 home 目录
cd ..                           # 返回上级目录
cd -                            # 回到上一个目录

mkdir new_dir                   # 创建目录
mkdir -p a/b/c                  # 递归创建多级目录

rmdir empty_dir                 # 删除空目录

tree                            # 以树状图显示目录结构（需安装：apt install tree）
tree -L 2                       # 只显示两层深度
```

---

## 查看文件内容

```bash
cat file.txt                    # 显示全部内容（适合短文件）

less file.txt                   # 分页查看（q 退出，/ 搜索，上下键翻页）
more file.txt                   # 类似 less，功能较少

head file.txt                   # 显示前 10 行
head -n 20 file.txt             # 显示前 20 行

tail file.txt                   # 显示后 10 行
tail -f file.txt                # 实时跟踪文件追加内容（查看日志常用）
tail -n 100 file.txt            # 显示后 100 行

wc file.txt                     # 统计行数、单词数、字节数
wc -l file.txt                  # 只统计行数
```

---

## 文件权限

```bash
# 权限组成：rwx rwx rwx = 所有者 用户组 其他人
# r=4, w=2, x=1

chmod 755 script.sh             # 设置权限：所有者 rwx，用户组 rx，其他人 rx
chmod +x script.sh              # 添加可执行权限
chmod -w file.txt               # 移除写入权限

chown user:group file.txt       # 修改文件所有者和用户组
chown -R user:group dir/        # 递归修改目录及其内容

umask                           # 查看默认权限掩码
```

---

## 查找搜索

```bash
# 查找文件
find /path -name "file.txt"                 # 按文件名查找
find /path -name "*.c"                      # 查找所有 .c 文件
find /path -size +100M                      # 查找大于 100MB 的文件
find /path -mtime -7                        # 查找 7 天内修改过的文件

# 搜索文件内容
grep "keyword" file.txt                     # 在文件中搜索关键词
grep -r "keyword" /path/                    # 递归搜索目录下所有文件
grep -i "keyword" file.txt                  # 忽略大小写
grep -n "keyword" file.txt                  # 显示匹配行号
grep -c "keyword" file.txt                  # 统计匹配次数

# 定位文件（需先更新数据库）
locate file.txt                             # 快速查找文件
sudo updatedb                               # 更新 locate 数据库
```

---

## 系统信息

```bash
uname -a                        # 查看内核版本
uname -r                        # 仅查看内核版本号

df -h                           # 查看磁盘使用情况（易读格式）
du -sh /path/                   # 查看目录总大小
du -h --max-depth=1             # 查看一级子目录大小

free -h                         # 查看内存使用情况

uptime                          # 查看系统运行时间

hostname                        # 查看主机名

whoami                          # 查看当前用户名

id                              # 查看当前用户和组信息

date                            # 查看当前日期时间
cal                             # 查看日历
```

---

## 进程管理

```bash
ps aux                          # 查看所有进程
ps -ef                          # 另一种格式查看进程

top                             # 实时进程监控（按 q 退出）
htop                            # 更友好的 top（需安装）

kill PID                        # 终止进程
kill -9 PID                     # 强制终止进程
killall process_name            # 按名称终止所有相关进程

nohup command &                 # 后台运行命令（关闭终端后继续运行）
jobs                            # 查看后台任务
fg %1                           # 将后台任务调到前台
bg %1                           # 将前台任务放到后台

sleep 5                         # 等待 5 秒
```

---

## 网络相关

```bash
ping 8.8.8.8                    # 测试网络连通性
ping -c 4 8.8.8.8               # 只发送 4 个包

curl https://example.com        # 发送 HTTP 请求
curl -O file.zip https://url    # 下载文件

wget https://url/file.zip       # 下载文件

ifconfig                        # 查看网络接口（可能需 net-tools）
ip a                            # 新版查看 IP 地址（推荐）
ip addr show                    # 同上

netstat -tuln                   # 查看端口监听状态
ss -tuln                        # 新版 netstat（推荐）

ssh user@192.168.1.100          # SSH 远程连接
scp file.txt user@ip:/path/     # 通过 SSH 复制文件
```

---

## 压缩归档

```bash
# tar（最常用）
tar -cvf archive.tar dir/       # 打包目录为 tar（不压缩）
tar -xvf archive.tar            # 解包 tar
tar -czvf archive.tar.gz dir/   # 打包并压缩为 gz
tar -xzvf archive.tar.gz        # 解压 tar.gz
tar -cjvf archive.tar.bz2 dir/  # 打包压缩为 bz2
tar -xjvf archive.tar.bz2       # 解压 tar.bz2

# zip
zip -r archive.zip dir/         # 压缩为 zip
unzip archive.zip               # 解压 zip

# gzip / gunzip
gzip file.txt                   # 压缩为 .gz（原文件消失）
gunzip file.txt.gz              # 解压
```

---

## 包管理 (apt)

适用于 Ubuntu / Debian 系列。

```bash
sudo apt update                 # 更新软件包列表（建议先执行这个）
sudo apt upgrade                # 升级所有已安装的包
sudo apt full-upgrade           # 升级包括依赖变更

sudo apt install package_name   # 安装软件包
sudo apt remove package_name    # 卸载软件包
sudo apt purge package_name     # 完全卸载（含配置文件）
sudo apt autoremove             # 卸载不需要的依赖

apt search keyword              # 搜索软件包
apt show package_name           # 查看软件包详细信息

sudo apt clean                  # 清理下载的缓存包

dpkg -l                         # 列出所有已安装的包
dpkg -i package.deb             # 安装本地 .deb 文件
```

---

## 文本处理

```bash
echo "hello"                    # 输出文本
echo "hello" > file.txt         # 写入文件（覆盖）
echo "hello" >> file.txt        # 追加到文件

sort file.txt                   # 排序
sort -n file.txt                # 按数值排序
sort -r file.txt                # 反向排序

uniq file.txt                   # 去重（连续重复行只保留一个）
uniq -c file.txt                # 去重并统计重复次数

cut -d',' -f1 file.csv          # 按逗号分割取第一列

awk '{print $1}' file.txt       # 打印每行第一列
awk '{sum+=$1} END {print sum}' # 计算第一列总和

sed 's/old/new/g' file.txt      # 替换所有 old 为 new（不修改原文件）
sed -i 's/old/new/g' file.txt   # 替换并直接修改文件

diff file1.txt file2.txt        # 比较两个文件差异
```

---

## 用户权限

```bash
sudo command                    # 以 root 权限执行命令
sudo -i                         # 切换到 root 用户
sudo su                         # 切换到 root 用户（加载 root 环境）
su - username                   # 切换到指定用户

passwd                          # 修改当前用户密码
sudo passwd username            # 修改指定用户密码

useradd newuser                 # 创建新用户（需要 sudo）
userdel username                # 删除用户（需要 sudo）
usermod -aG group username      # 将用户添加到组

adduser newuser                 # 更友好的创建用户方式（推荐）
deluser username                # 删除用户
```

---

## 实用技巧

```bash
# 管道：将一个命令的输出作为下一个命令的输入
ls -la | grep ".txt"            # 只显示 .txt 文件
dmesg | grep error              # 查看内核错误日志

# 重定向
command > file.txt              # 输出到文件（覆盖）
command >> file.txt             # 输出到文件（追加）
command 2>&1                    # 将错误输出也重定向到标准输出

# 通配符
*.txt                           # 所有 .txt 文件
file?.txt                       # file1.txt, file2.txt 等
file[0-9].txt                   # file0.txt 到 file9.txt

# 别名
alias ll='ls -la'               # 设置快捷命令
alias gs='git status'
unalias ll                      # 取消别名

# 环境变量
export PATH=$PATH:/new/path     # 添加路径到 PATH
echo $PATH                      # 查看 PATH
env                             # 查看所有环境变量

# 查看命令位置
which gcc                       # 查看 gcc 命令的路径
whereis gcc                     # 查看 gcc 的二进制、源码、man 路径

# 查看命令帮助
command --help                  # 查看命令帮助
man command                     # 查看命令手册（q 退出）
info command                    # 查看命令 info 文档

# 清屏
clear                           # 清屏（或 Ctrl+L）

# 历史命令
history                         # 查看命令历史
!!                              # 执行上一条命令
!123                            # 执行历史中第 123 条命令
Ctrl + r                        # 搜索历史命令（输入关键词）
```

---

## 文件系统层次

WSL / Ubuntu 的文件系统结构：

| 路径 | 说明 |
|------|------|
| `/` | 根目录 |
| `/home/` | 用户主目录（类似 Windows 的 `C:\Users`） |
| `/etc/` | 系统配置文件 |
| `/var/` | 日志、缓存等可变数据 |
| `/tmp/` | 临时文件（重启清空） |
| `/usr/` | 用户程序和数据 |
| `/bin/`、`/usr/bin/` | 可执行命令 |
| `/mnt/` | 挂载点（WSL 中 Windows 的 C/D 盘在 `/mnt/c/`、`/mnt/d/`） |
| `/opt/` | 可选软件包 |
| `/root/` | root 用户的 home 目录 |

> 在 WSL 中，Windows 的 C 盘路径为 `/mnt/c/`，D 盘为 `/mnt/d/`。

---

*最后更新：2026-05-15*
