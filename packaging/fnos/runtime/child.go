package main

import (
	"errors"
	"fmt"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
	"time"
)

type childProcess struct {
	app, state string
	cmd        *exec.Cmd
	done       chan struct{}
	failure    string
}

func (p *childProcess) running() bool {
	if p.done == nil {
		return false
	}
	select {
	case <-p.done:
		return false
	default:
		return true
	}
}

func (p *childProcess) start(s settings) error {
	p.failure = ""
	if !s.AllowRemote {
		return nil
	}
	if !accepted(p.state) {
		p.failure = "设备所有者授权尚未确认"
		return errors.New(p.failure)
	}
	if err := privateWrite(filepath.Join(p.state, "conf", "common.ini"), s.commonINI(p.app)); err != nil {
		return err
	}
	env := make([]string, 0, len(os.Environ())+4)
	for _, v := range os.Environ() {
		key, _, _ := strings.Cut(v, "=")
		switch key {
		case "DISPLAY", "WAYLAND_DISPLAY", "HOME", "AIRAN_DESK_STATE_DIR", "AIRAN_DESK_SKIP_UINPUT_SETUP", "AIRAN_DESK_LOG_DIR":
			continue
		}
		env = append(env, v)
	}
	env = append(env, "HOME="+filepath.Join(p.state, "home"), "AIRAN_DESK_STATE_DIR="+p.state,
		"AIRAN_DESK_SKIP_UINPUT_SETUP=1", "AIRAN_DESK_LOG_DIR="+filepath.Join(p.state, "logs"))
	cmd := exec.Command(filepath.Join(p.app, "airan", "airan-desk"))
	cmd.Dir = filepath.Join(p.app, "airan")
	cmd.Env = env
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	cmd.SysProcAttr = &syscall.SysProcAttr{Setpgid: true, Pdeathsig: syscall.SIGKILL}
	if err := cmd.Start(); err != nil {
		log.Printf("start Airan: %v", err)
		p.failure = "客户端无法启动，请查看应用日志"
		return errors.New(p.failure)
	}
	p.cmd = cmd
	p.done = make(chan struct{})
	go func(done chan struct{}) {
		err := cmd.Wait()
		if err != nil {
			log.Printf("Airan exited: %v", err)
		}
		close(done)
	}(p.done)
	timer := time.NewTimer(1200 * time.Millisecond)
	defer timer.Stop()
	select {
	case <-p.done:
		p.failure = "客户端启动后退出，请查看应用日志"
		return errors.New(p.failure)
	case <-timer.C:
		return nil
	}
}

func (p *childProcess) status() string {
	if p.running() {
		return "进程运行中（不代表信令已连接）"
	}
	if p.failure != "" {
		return p.failure
	}
	return "已停止"
}

type procIdentity struct {
	pid, parent int
	started     string
}

func readProcess(pid int) (procIdentity, error) {
	data, err := os.ReadFile(fmt.Sprintf("/proc/%d/stat", pid))
	if err != nil {
		return procIdentity{}, err
	}
	_, rest, ok := strings.Cut(string(data), ") ")
	fields := strings.Fields(rest)
	if !ok || len(fields) < 20 {
		return procIdentity{}, errors.New("invalid process stat")
	}
	parent, err := strconv.Atoi(fields[1])
	if err != nil {
		return procIdentity{}, err
	}
	return procIdentity{pid, parent, fields[19]}, nil
}

func descendants(pid int) []procIdentity {
	entries, _ := os.ReadDir("/proc")
	all := make([]procIdentity, 0)
	for _, e := range entries {
		n, err := strconv.Atoi(e.Name())
		if err != nil {
			continue
		}
		if p, err := readProcess(n); err == nil {
			all = append(all, p)
		}
	}
	known := map[int]bool{pid: true}
	var result []procIdentity
	for changed := true; changed; {
		changed = false
		for _, p := range all {
			if known[p.parent] && !known[p.pid] {
				known[p.pid] = true
				result = append(result, p)
				changed = true
			}
		}
	}
	return result
}

func signalKnown(p procIdentity, sig syscall.Signal) {
	if current, err := readProcess(p.pid); err == nil && current.started == p.started {
		_ = syscall.Kill(p.pid, sig)
	}
}

func (p *childProcess) stop() {
	if !p.running() {
		return
	}
	pid := p.cmd.Process.Pid
	desc := descendants(pid)
	// Native Airan currently has no POSIX graceful-exit bridge. Bound shutdown and
	// terminate known descendants too, including forkpty children in separate sessions.
	for i := len(desc) - 1; i >= 0; i-- {
		signalKnown(desc[i], syscall.SIGTERM)
	}
	_ = syscall.Kill(-pid, syscall.SIGTERM)
	select {
	case <-p.done:
	case <-time.After(3500 * time.Millisecond):
		log.Printf("Airan did not exit after SIGTERM; terminating its process group")
		_ = syscall.Kill(-pid, syscall.SIGKILL)
		<-p.done
	}
	for i := len(desc) - 1; i >= 0; i-- {
		signalKnown(desc[i], syscall.SIGKILL)
	}
}

func (p *childProcess) apply(old, next settings) error {
	p.stop()
	err := p.start(next)
	if err == nil {
		err = saveSettings(p.state, next)
	}
	if err == nil {
		// Disabled settings are persisted too, without launching the client.
		err = privateWrite(filepath.Join(p.state, "conf", "common.ini"), next.commonINI(p.app))
	}
	if err == nil {
		return nil
	}
	log.Printf("apply settings: %v", err)
	p.stop()
	if restore := privateWrite(filepath.Join(p.state, "conf", "common.ini"), old.commonINI(p.app)); restore != nil {
		log.Printf("restore config: %v", restore)
	}
	_ = saveSettings(p.state, old)
	if restore := p.start(old); restore != nil {
		return errors.New("设置未生效，恢复进程失败；请查看应用日志")
	}
	return errors.New("设置未生效，已恢复原配置")
}

func (p *childProcess) resetPassword(s settings) error {
	p.stop()
	old, err := readIdentity(p.state)
	if err != nil {
		_ = p.start(s)
		return err
	}
	next := old
	next.Password, err = uuid()
	if err == nil {
		err = writeIdentity(p.state, next)
	}
	if err == nil {
		err = p.start(s)
	}
	if err == nil {
		return nil
	}
	p.stop()
	_ = writeIdentity(p.state, old)
	_ = p.start(s)
	return errors.New("验证码更换失败，已尝试恢复原验证码")
}
