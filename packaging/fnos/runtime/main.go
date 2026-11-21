package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"syscall"
	"time"
)

type gatewayListener struct{ *net.UnixListener }

func (l gatewayListener) Accept() (net.Conn, error) {
	for {
		conn, err := l.AcceptUnix()
		if err != nil {
			return nil, err
		}
		if trustedPeer(conn) {
			return conn, nil
		}
		conn.Close()
	}
}
func trustedPeer(conn *net.UnixConn) bool {
	raw, err := conn.SyscallConn()
	if err != nil {
		return false
	}
	var peer *syscall.Ucred
	var peerErr error
	err = raw.Control(func(fd uintptr) {
		peer, peerErr = syscall.GetsockoptUcred(int(fd), syscall.SOL_SOCKET, syscall.SO_PEERCRED)
	})
	return err == nil && peerErr == nil && peer != nil && (peer.Uid == 0 || peer.Uid == uint32(os.Geteuid()))
}
func openGateway(path string) (gatewayListener, error) {
	fi, err := os.Lstat(path)
	if err == nil {
		stat, owned := fi.Sys().(*syscall.Stat_t)
		if fi.Mode()&os.ModeSocket == 0 || !owned || stat.Uid != uint32(os.Geteuid()) {
			return gatewayListener{}, errors.New("socket is not application-owned")
		}
		conn, err := net.DialTimeout("unix", path, 300*time.Millisecond)
		if err == nil {
			conn.Close()
			return gatewayListener{}, errors.New("socket already in use")
		}
		if !errors.Is(err, syscall.ECONNREFUSED) {
			return gatewayListener{}, err
		}
		if err = os.Remove(path); err != nil {
			return gatewayListener{}, err
		}
	} else if !errors.Is(err, os.ErrNotExist) {
		return gatewayListener{}, err
	}
	ln, err := net.ListenUnix("unix", &net.UnixAddr{Name: path, Net: "unix"})
	if err != nil {
		return gatewayListener{}, err
	}
	if err = os.Chmod(path, 0600); err != nil {
		ln.Close()
		return gatewayListener{}, err
	}
	return gatewayListener{ln}, nil
}

func run(app, state, socket string) error {
	for _, p := range []string{app, state, socket} {
		if !filepath.IsAbs(p) {
			return errors.New("app, state and socket paths must be absolute")
		}
	}
	syscall.Umask(0077)
	if err := os.MkdirAll(state, 0700); err != nil {
		return err
	}
	lock, err := os.OpenFile(filepath.Join(state, "manager.lock"), os.O_CREATE|os.O_RDWR, 0600)
	if err != nil {
		return err
	}
	defer lock.Close()
	if err = syscall.Flock(int(lock.Fd()), syscall.LOCK_EX|syscall.LOCK_NB); err != nil {
		return fmt.Errorf("lock state: %w", err)
	}
	if err = prepareLayout(app, state); err != nil {
		return err
	}
	if err = ensureIdentity(state); err != nil {
		return err
	}
	s, err := loadSettings(state)
	if err != nil {
		return err
	}
	if err = privateWrite(filepath.Join(state, "conf", "common.ini"), s.commonINI(app)); err != nil {
		return err
	}
	m, err := newManager(s, app, state)
	if err != nil {
		return err
	}
	ln, err := openGateway(socket)
	if err != nil {
		return err
	}
	defer ln.Close()
	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGTERM, os.Interrupt)
	defer stop()
	if err = m.child.start(s); err != nil {
		log.Printf("Airan startup: %v", err)
	}
	defer func() { m.mu.Lock(); defer m.mu.Unlock(); m.child.stop() }()
	server := &http.Server{Handler: m, ReadHeaderTimeout: 4 * time.Second, ReadTimeout: 6 * time.Second,
		WriteTimeout: 24 * time.Second, IdleTimeout: 20 * time.Second, MaxHeaderBytes: 16384}
	serve := make(chan error, 1)
	go func() { serve <- server.Serve(ln) }()
	select {
	case err := <-serve:
		_ = server.Close()
		return err
	case <-ctx.Done():
		deadline, cancel := context.WithTimeout(context.Background(), 12*time.Second)
		defer cancel()
		if err = server.Shutdown(deadline); err != nil {
			_ = server.Close()
		}
		return nil
	}
}
func main() {
	if len(os.Args) > 1 && os.Args[1] == "--notify" {
		if err := notify(os.Args[2:]); err != nil {
			log.Fatal(err)
		}
		return
	}
	app := flag.String("app-dir", "", "absolute path of the fnOS application")
	state := flag.String("state-dir", "", "absolute persistent state path")
	socket := flag.String("socket", "", "absolute Unix socket path")
	flag.Parse()
	if err := run(*app, *state, *socket); err != nil {
		log.Fatal(err)
	}
}
