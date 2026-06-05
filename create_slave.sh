#!/bin/bash

# ==============================================================================
# Ubuntu Worker Node Setup Script for Omarchy Cluster
# ==============================================================================

# --- CONFIGURATION ---
# IMPORTANT: Verify this UID matches the `id mpiu` output from your Arch Master!
MPI_UID="1500"
MASTER_HOSTNAME="ub0"
MASTER_IP="100.67.54.127"
# ---------------------

# 1. Root Check
if [ "$EUID" -ne 0 ]; then
  echo "❌ Please run this script with sudo: sudo ./setup_worker.sh"
  exit 1
fi

echo "🚀 Starting Ubuntu Worker Node Setup..."

# 2. Install Dependencies
echo "📦 Installing OpenMPI and NFS client..."
# apt-get update
apt-get install -y openmpi-bin openmpi-common libopenmpi-dev nfs-common

# 3. Install Tailscale
if ! command -v tailscale &>/dev/null; then
  echo "🌐 Installing Tailscale..."
  curl -fsSL https://tailscale.com/install.sh | sh
  echo "⚠️ Starting Tailscale. Check for a login link below if this is a new machine!"
  tailscale up
else
  echo "✅ Tailscale is already installed."
  tailscale up
fi

# 4. Create Cluster User
if id "mpiu" &>/dev/null; then
  echo "👤 User 'mpiu' already exists."
else
  echo "👤 Creating 'mpiu' user with UID $MPI_UID..."
  useradd -M -d /mirror -s /bin/bash -u $MPI_UID mpiu
  echo "🔑 Please type a new password for the mpiu user:"
  passwd mpiu
fi

# 5. Set up the NFS Mount
echo "📂 Configuring NFS /mirror directory..."
mkdir -p /mirror

# Temporarily map the master in hosts just so the mount command works
if ! grep -q "$MASTER_HOSTNAME" /etc/hosts; then
  # Define the target file
  TARGET_FILE="/etc/hosts"

  # Use cat and EOF to append everything into the file
  cat <<EOF >>"$TARGET_FILE"
#Desk
1111111111111 ub0
#Thinkpad
1111111111111 ub1
#juanda
11111111111111 ub2
#cova
11111111111111 ub3
EOF

  echo "Text appended successfully!"
  echo "📄 CURRENT HOSTS FILE"
  cat /etc/hosts
  echo "==============================================================="
fi

if ! mountpoint -q /mirror; then
  mount -t nfs ${MASTER_HOSTNAME}:/mirror /mirror
  echo "✅ Mounted /mirror from ${MASTER_HOSTNAME}"
else
  echo "✅ /mirror is already mounted."
fi

# Make the mount permanent across reboots
if ! grep -q "${MASTER_HOSTNAME}:/mirror" /etc/fstab; then
  cp /etc/fstab ./fstab.bak
  echo "${MASTER_HOSTNAME}:/mirror /mirror nfs defaults 0 0" >>/etc/fstab
  echo "✅ Added /mirror to /etc/fstab for automatic mounting on reboot."
  echo "Current /etc/fstab"
  cat /etc/fstab
  echo "==============================================================="
fi
#
# # 6. OpenMPI Network Routing (Force Tailscale)
# echo "🔒 Configuring OpenMPI to strictly use tailscale0..."
# mkdir -p /etc/openmpi
# cat <<EOF > /etc/openmpi/openmpi-mca-params.conf
# pml = ob1
# btl_tcp_if_include = tailscale0
# oob_tcp_if_include = tailscale0
# prte_oob_tcp_if_include = tailscale0
# EOF

# ln -sf /etc/openmpi/openmpi-mca-params.conf /etc/openmpi-mca-params.conf
#
# # 7. Wrap up
# LOCAL_IP=$(ip -4 addr show tailscale0 | grep -oP '(?<=inet\s)\d+(\.\d+){3}')

echo ""
echo "🎉 Script Complete! 🎉"
echo "====================================================================="
echo "⚠️ IMPORTANT MANUAL NEXT STEPS:"
echo "1. Go to your ARCH MASTER NODE."
echo "2. Add this VM's IP to the Arch master's /etc/hosts: $LOCAL_IP  ub_new"
echo "3. Add this VM to your /mirror/machinefile so OpenMPI gives it work!"
echo "====================================================================="
