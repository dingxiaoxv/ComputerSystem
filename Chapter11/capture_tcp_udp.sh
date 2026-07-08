#!/usr/bin/env bash
# TCP/UDP 抓包一键脚本 —— sudo bash Chapter11/capture_tcp_udp.sh
# 输出：同目录 capture_results.txt；pcap 文件放 /tmp/netcap/。
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$SCRIPT_DIR/capture_results.txt"
PCAP_DIR="/tmp/netcap"
TARGET="neverssl.com"        # 永远纯 HTTP、不跳 HTTPS
DNS_NAME="example.org"       # UDP/DNS 样本
DNS_SERVER="8.8.8.8"

if [ "$(id -u)" -ne 0 ]; then
  echo "请用 sudo 运行：sudo bash $0"
  exit 1
fi

mkdir -p "$PCAP_DIR"
: > "$OUT"

sec() { echo -e "\n\n########## $* ##########" | tee -a "$OUT"; }
note() { echo "# $*" | tee -a "$OUT"; }

capture() {   # $1=pcap $2=bpf过滤 $3=触发命令
  local pcap="$1"
  local filter="$2"
  local trigger="$3"

  tcpdump -i any -nn "$filter" -w "$pcap" 2>>"$OUT" &
  local tp=$!
  sleep 1
  bash -lc "$trigger" >>"$OUT" 2>&1 || true
  sleep 2
  kill -INT "$tp" 2>/dev/null || true
  wait "$tp" 2>/dev/null || true
}

sec "ENV"
{
  echo "date: $(date -Is)"
  echo "kernel: $(uname -r)"
  echo "tools:"
  command -v tcpdump || true
  command -v curl || true
  command -v dig || true
  command -v tc || true
  echo "interfaces:"
  ip -o -4 addr show | awk '{print "  "$2, $4}'
} 2>&1 | tee -a "$OUT"

IP="$(dig +short "$TARGET" 2>/dev/null | grep -E '^[0-9]+\.' | head -1 || true)"
sec "ROUTE"
echo "target=$TARGET resolved_ip=${IP:-<empty>}" | tee -a "$OUT"
if [ -z "${IP:-}" ]; then
  note "解析 $TARGET 失败，无法继续 TCP 抓包"
  exit 1
fi
IFACE="$(ip route get "$IP" 2>/dev/null | grep -oP 'dev \K\S+' | head -1 || true)"
echo "egress_iface=${IFACE:-<empty>}" | tee -a "$OUT"
ip route get "$IP" 2>&1 | tee -a "$OUT" || true

# 实验1：TCP 完整生命周期
sec "EXP1 capture (curl http://$TARGET via $IP)"
capture "$PCAP_DIR/tcp.pcap" "host $IP and tcp port 80" \
  "curl -sS --max-time 15 --resolve $TARGET:80:$IP http://$TARGET/ -o /dev/null"

sec "EXP1-A  TCP 相对序号（seq 从1开始，看三次握手、数据、挥手）"
tcpdump -nnvv -r "$PCAP_DIR/tcp.pcap" 2>>"$OUT" | tee -a "$OUT" || true

sec "EXP1-B  TCP 绝对序号（真实随机 ISN）"
tcpdump -nnvvS -r "$PCAP_DIR/tcp.pcap" 2>>"$OUT" | tee -a "$OUT" || true

sec "EXP1-C  HTTP 明文 payload（PSH 数据段 hex+ascii）"
tcpdump -nnX -r "$PCAP_DIR/tcp.pcap" 'tcp[13] & 8 != 0' 2>>"$OUT" | tee -a "$OUT" || true

# 实验2：UDP 无连接（DNS）
sec "EXP2 capture (dig $DNS_NAME @$DNS_SERVER)"
capture "$PCAP_DIR/udp.pcap" "udp port 53 and host $DNS_SERVER" \
  "dig +short $DNS_NAME @$DNS_SERVER"

sec "EXP2  UDP/DNS 报文（hex+ascii）"
tcpdump -nnvvX -r "$PCAP_DIR/udp.pcap" 2>>"$OUT" | tee -a "$OUT" || true

# 实验3：TCP 重传（netem 丢包，可能因权限/驱动/VPN 而不可用）
if [ -n "${IFACE:-}" ]; then
  sec "EXP3 capture (netem loss 30% on $IFACE)"
  cleanup_netem() { tc qdisc del dev "$IFACE" root 2>/dev/null || true; }
  trap cleanup_netem EXIT

  if tc qdisc add dev "$IFACE" root netem loss 30% 2>>"$OUT"; then
    note "netem 已注入到 $IFACE"
    capture "$PCAP_DIR/retx.pcap" "host $IP and tcp port 80" \
      "curl -sS --max-time 20 --resolve $TARGET:80:$IP http://$TARGET/ -o /dev/null"
    cleanup_netem
    trap - EXIT
    note "netem 已删除，网络恢复"

    sec "EXP3  重传视图（前120行；找重复 seq / Retransmission / RTO 痕迹）"
    tcpdump -nnvv -r "$PCAP_DIR/retx.pcap" 2>>"$OUT" | sed -n '1,120p' | tee -a "$OUT" || true
  else
    note "netem 注入失败，跳过重传实验；可能是接口不支持或已有 qdisc"
    cleanup_netem
    trap - EXIT
  fi
else
  sec "EXP3 skipped"
  note "未能确定出口网卡"
fi

chown "${SUDO_USER:-root}:${SUDO_USER:-root}" "$OUT" 2>/dev/null || true
sec "DONE"
echo "结果文件：$OUT" | tee -a "$OUT"
echo "pcap 目录：$PCAP_DIR" | tee -a "$OUT"
echo "行数：$(wc -l < "$OUT")" | tee -a "$OUT"
