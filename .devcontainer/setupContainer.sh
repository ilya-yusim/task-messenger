#!/bin/bash
set -euo pipefail

echo "Updating package lists..."
sudo apt-get update

echo "Installing required packages (GCC 13 + legacy GCC 10)..."
sudo apt-get install -y python3-pip libboost-all-dev gcc-13 g++-13 gcc-10 g++-10 ninja-build

echo "Configuring GCC alternatives (setting GCC 13 as default)..."
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-10 100
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 200
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-10 100
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 200

echo "Installing Meson..."
pip3 install --upgrade meson

echo "GCC version now:"
gcc --version | head -n1
echo "Setup complete."