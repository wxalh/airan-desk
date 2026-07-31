#!/usr/bin/env sh
set -u

rule_name="60-airan-desk-uinput.rules"
rule_target="${AIRAN_DESK_UINPUT_RULE_TARGET:-/etc/udev/rules.d/$rule_name}"

if [ "${AIRAN_DESK_SKIP_UINPUT_SETUP:-0}" = "1" ]; then
    exit 0
fi

notify_user()
{
    message=$1
    printf '%s\n' "$message" >&2
    if [ -n "${DBUS_SESSION_BUS_ADDRESS:-}" ] && command -v notify-send >/dev/null 2>&1; then
        notify-send "Airan Desk" "$message" >/dev/null 2>&1 || true
    fi
}

input_group_exists()
{
    if command -v getent >/dev/null 2>&1; then
        getent group input >/dev/null 2>&1
    else
        grep -q '^input:' /etc/group 2>/dev/null
    fi
}

user_in_input_group()
{
    id -nG "$1" 2>/dev/null | tr ' ' '\n' | grep -qx input
}

install_rule()
{
    source_file=$1
    target_file=$2
    target_user=$3
    if [ "$(id -u)" -ne 0 ]; then
        echo "Installing the uinput rule requires root privileges." >&2
        return 1
    fi

    if [ -n "$target_user" ] && [ "$target_user" != "root" ]; then
        if ! id "$target_user" >/dev/null 2>&1; then
            echo "Cannot grant uinput access to unknown user: $target_user" >&2
            return 1
        fi
        if ! input_group_exists; then
            if ! command -v groupadd >/dev/null 2>&1; then
                echo "The input group does not exist and groupadd is unavailable." >&2
                return 1
            fi
            groupadd --system input
        fi
        if ! user_in_input_group "$target_user"; then
            if ! command -v usermod >/dev/null 2>&1; then
                echo "usermod is unavailable; cannot add $target_user to the input group." >&2
                return 1
            fi
            usermod -a -G input "$target_user"
        fi
    fi

    install -d "$(dirname "$target_file")"
    install -m 0644 "$source_file" "$target_file"

    if [ ! -e /dev/uinput ] && [ ! -e /dev/input/uinput ]; then
        if ! command -v modprobe >/dev/null 2>&1; then
            echo "modprobe is unavailable; cannot load the uinput kernel module." >&2
            return 1
        fi
        modprobe uinput >/dev/null 2>&1 || true
    fi
    if command -v udevadm >/dev/null 2>&1; then
        udevadm control --reload-rules >/dev/null 2>&1
        udevadm trigger --subsystem-match=misc --action=add >/dev/null 2>&1
        udevadm settle >/dev/null 2>&1 || true
    fi

    wait_count=0
    while [ ! -e /dev/uinput ] && [ ! -e /dev/input/uinput ] && [ "$wait_count" -lt 20 ]; do
        sleep 0.1
        wait_count=$((wait_count + 1))
    done
    if [ ! -e /dev/uinput ] && [ ! -e /dev/input/uinput ]; then
        echo "The uinput device was not created. Install the uinput module for the running kernel, then retry." >&2
        return 1
    fi
}

if [ "${1:-}" = "--install" ]; then
    [ $# -eq 4 ] || exit 2
    install_rule "$2" "$3" "$4"
    exit $?
fi

rule_source=${1:-}
if [ -z "$rule_source" ] || [ ! -f "$rule_source" ]; then
    exit 0
fi
target_user=${AIRAN_DESK_UINPUT_USER:-${SUDO_USER:-}}
if [ -z "$target_user" ] && [ "$(id -u)" -ne 0 ]; then
    target_user=$(id -un)
fi

device_path=""
if [ -e /dev/uinput ]; then
    device_path=/dev/uinput
elif [ -e /dev/input/uinput ]; then
    device_path=/dev/input/uinput
fi
user_has_group=1
if [ -n "$target_user" ] && [ "$target_user" != "root" ]; then
    if ! user_in_input_group "$target_user"; then
        user_has_group=0
    fi
fi
if [ -f "$rule_target" ] && cmp -s "$rule_source" "$rule_target" && \
   [ "$user_has_group" -eq 1 ] && [ -n "$device_path" ] && [ -w "$device_path" ]; then
    exit 0
fi

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
script_path="$script_dir/$(basename -- "$0")"
installed=0
if [ "$(id -u)" -eq 0 ]; then
    install_rule "$rule_source" "$rule_target" "$target_user" && installed=1
elif [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] && command -v pkexec >/dev/null 2>&1; then
    if pkexec "$script_path" --install "$rule_source" "$rule_target" "$target_user"; then
        installed=1
    fi
elif [ -t 0 ] && command -v sudo >/dev/null 2>&1; then
    if sudo "$script_path" --install "$rule_source" "$rule_target" "$target_user"; then
        installed=1
    fi
fi

if [ "$installed" -eq 1 ]; then
    case "${LANG:-}" in
        zh*) notify_user "uinput 权限配置已完成，当前用户已加入 input 组。请注销后重新登录，或者重启系统，再使用远程输入。" ;;
        *) notify_user "The uinput permission setup completed and the current user was added to the input group. Log out and back in, or restart the system, before using remote input." ;;
    esac
    exit 0
fi

case "${LANG:-}" in
    zh*) notify_user "未能完成 uinput 权限配置。请确认当前内核已提供 uinput 模块，并将用户 '$target_user' 加入 input 组，然后注销后重新登录或重启系统。" ;;
    *) notify_user "The uinput permission setup did not complete. Ensure the running kernel provides the uinput module and add user '$target_user' to the input group, then log out and back in or restart." ;;
esac
exit 0
