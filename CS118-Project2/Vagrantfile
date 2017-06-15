# -*- mode: ruby -*-
# vi: set ft=ruby :

$INSTALL_BASE = <<SCRIPT
  sudo apt-get update
  sudo apt-get install -y build-essential vim emacs

  echo "tc qdisc add dev enp0s8 root netem loss 10% delay 20ms" > /set-loss.sh
  chmod 755 /set-loss.sh
SCRIPT

Vagrant.configure(2) do |config|
  config.vm.box = "boxcutter/ubuntu1604"
  config.vm.provision "shell", inline: $INSTALL_BASE

  # config.vm.provider "virtualbox" do |vb|
  #   # Display the VirtualBox GUI when booting the machine
  #   vb.gui = true
  #
  #   # Customize the amount of memory on the VM:
  #   vb.memory = "1024"
  # end

  config.vm.define :client, primary: true do |host|
    host.vm.hostname = "client"
    host.vm.network "private_network", ip: "10.0.0.2", netmask: "255.255.255.0",
                    virtualbox__intnet: "cs118"
  end

  config.vm.define :server do |host|
    host.vm.hostname = "server"
    host.vm.network "private_network", ip: "10.0.0.1", netmask: "255.255.255.0",
                    virtualbox__intnet: "cs118"
  end
end
