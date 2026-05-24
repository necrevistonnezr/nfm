# ~/.bashrc snippet – gofile-downloader via vopono + uv
# In .bashrc einfügen: source ~/dotfiles/gofile.bash

gofile() {
    local _uv
    _uv="$(command -v uv)"
    cd ~/gofile-downloader || return 1
    sudo vopono exec \
        --provider privateinternetaccess \
        --dns 1.1.1.1 \
        -i enp1s0 \
        --firewall iptables \
        --server "" \
        "${_uv} run gofile-downloader.py $1"
    cd /media/SG2TB/M/ || return 1
}
