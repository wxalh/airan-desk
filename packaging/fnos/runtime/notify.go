package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"syscall"
	"time"
)

type notification struct {
	Time   string
	Event  string
	Peer   string
	Detail string
}

func (n notification) Label() string {
	switch n.Event {
	case "connection_requested":
		return "收到连接请求"
	case "authentication_failed":
		return "连接验证码验证失败"
	case "connection_established":
		return "连接已建立"
	case "connection_disconnected":
		return "连接已断开"
	case "test":
		return "测试通知"
	default:
		return n.Event
	}
}

var peerPattern = regexp.MustCompile("^[A-Za-z0-9_.:-]{1,128}$")

func recordNotification(state, event, peer, detail string) error {
	switch event {
	case "connection_requested", "authentication_failed", "connection_established", "connection_disconnected", "test":
	default:
		return errors.New("invalid notification event")
	}
	if !filepath.IsAbs(state) || !peerPattern.MatchString(peer) || len(detail) > 512 || strings.ContainsAny(detail, "\x00\r\n") {
		return errors.New("invalid notification arguments")
	}
	lock, err := os.OpenFile(filepath.Join(state, "notifications.lock"), os.O_CREATE|os.O_RDWR, 0600)
	if err != nil {
		return err
	}
	defer lock.Close()
	if err = syscall.Flock(int(lock.Fd()), syscall.LOCK_EX); err != nil {
		return err
	}
	path := filepath.Join(state, "notifications.jsonl")
	if info, err := os.Stat(path); err == nil && info.Size() > 512<<10 {
		if err = os.Rename(path, path+".1"); err != nil {
			return err
		}
	}
	out, err := os.OpenFile(path, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0600)
	if err != nil {
		return err
	}
	defer out.Close()
	data, err := json.Marshal(notification{time.Now().Format(time.RFC3339), event, peer, detail})
	if err != nil {
		return err
	}
	if _, err = out.Write(append(data, '\n')); err != nil {
		return err
	}
	return out.Sync()
}

func recentNotifications(state string) []notification {
	data, err := os.ReadFile(filepath.Join(state, "notifications.jsonl"))
	if err != nil {
		return nil
	}
	lines := strings.Split(strings.TrimSpace(string(data)), "\n")
	if len(lines) > 20 {
		lines = lines[len(lines)-20:]
	}
	result := make([]notification, 0, len(lines))
	for i := len(lines) - 1; i >= 0; i-- {
		var item notification
		if json.Unmarshal([]byte(lines[i]), &item) == nil {
			result = append(result, item)
		}
	}
	return result
}

func notify(args []string) error {
	if len(args) != 3 {
		return errors.New("usage: fnos-manager --notify event peer detail")
	}
	if err := recordNotification(os.Getenv("AIRAN_DESK_STATE_DIR"), args[0], args[1], args[2]); err != nil {
		return fmt.Errorf("notification delivery: %w", err)
	}
	return nil
}
