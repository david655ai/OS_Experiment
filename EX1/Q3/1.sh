make
sudo insmod process_usage_module.ko pid=1
sudo dmesg | tail -n 30
sudo rmmod process_usage_module