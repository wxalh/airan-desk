package main

import (
	"crypto/rand"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/url"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
)

type settings struct {
	AllowRemote bool   `json:"allow_remote"`
	WSServer    string `json:"ws_url"`
	ICEHost     string `json:"ice_host"`
	ICEPort     int    `json:"ice_port"`
	ICEUser     string `json:"ice_username"`
	ICEPassword string `json:"ice_password"`
}

func (s settings) validate() error {
	for _, value := range []string{s.WSServer, s.ICEHost, s.ICEUser, s.ICEPassword} {
		if len(value) > 2048 || strings.ContainsAny(value, "\x00\r\n") {
			return errors.New("配置值包含无效字符或超过长度限制")
		}
	}
	if s.AllowRemote && s.WSServer == "" {
		return errors.New("启用前请填写信令服务器地址")
	}
	if s.WSServer != "" {
		u, err := url.Parse(s.WSServer)
		if err != nil || (u.Scheme != "ws" && u.Scheme != "wss") || u.Hostname() == "" ||
			u.User != nil || u.Fragment != "" || strings.ContainsAny(s.WSServer, " \t") {
			return errors.New("信令地址须为 ws:// 或 wss://，不可包含用户名、密码或片段")
		}
		if u.Port() != "" {
			port, err := strconv.Atoi(u.Port())
			if err != nil || port < 1 || port > 65535 {
				return errors.New("信令端口无效")
			}
		}
	}
	if s.ICEPort < 1 || s.ICEPort > 65535 {
		return errors.New("STUN / TURN 端口须为 1–65535")
	}
	if s.ICEHost != "" && net.ParseIP(s.ICEHost) == nil && !hostnamePattern.MatchString(s.ICEHost) {
		return errors.New("STUN / TURN 地址请填写主机名或 IP，不含协议和端口")
	}
	return nil
}

var hostnamePattern = regexp.MustCompile("^[A-Za-z0-9](?:[A-Za-z0-9.-]{0,251}[A-Za-z0-9])?$")
var uuidPattern = regexp.MustCompile("^[A-Fa-f0-9]{8}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{4}-[A-Fa-f0-9]{12}$")

func validUUID(value string) bool {
	// Match the native client's QUuid validation, which rejects the null UUID.
	return uuidPattern.MatchString(value) && value != "00000000-0000-0000-0000-000000000000"
}

func uuid() (string, error) {
	var b [16]byte
	if _, err := rand.Read(b[:]); err != nil {
		return "", err
	}
	b[6], b[8] = (b[6]&15)|64, (b[8]&63)|128
	return strings.ToUpper(fmt.Sprintf("%x-%x-%x-%x-%x", b[:4], b[4:6], b[6:8], b[8:10], b[10:])), nil
}

func iniString(value string) string {
	if strings.HasPrefix(value, "@") {
		value = "@" + value
	}
	return "\"" + strings.NewReplacer("\\", "\\\\", "\"", "\\\"", "\n", "\\n", "\r", "\\r").Replace(value) + "\""
}

func (s settings) commonINI(app string) []byte {
	return []byte(fmt.Sprintf("[local]\nallowRemote=%t\nautoStart=false\nlogLevel=info\nlanguage=auto\n"+
		"\n[notification]\nnotify_script=%s\n\n[signal_server]\nwsUrl=%s\n\n[ice_server]\nhost=%s\nport=%d\nusername=%s\npassword=%s\n"+
		"\n[codec]\nopenh264Enabled=false\nopenh264Library=\n",
		s.AllowRemote, iniString(filepath.Join(app, "bin", "notify")), iniString(s.WSServer),
		iniString(s.ICEHost), s.ICEPort, iniString(s.ICEUser), iniString(s.ICEPassword)))
}

func privateWrite(path string, data []byte) error {
	f, err := os.CreateTemp(filepath.Dir(path), ".fnos-")
	if err != nil {
		return err
	}
	defer os.Remove(f.Name())
	if _, err = f.Write(data); err == nil {
		err = f.Sync()
	}
	if e := f.Close(); err == nil {
		err = e
	}
	if err != nil {
		return err
	}
	return os.Rename(f.Name(), path)
}

func saveSettings(dir string, s settings) error {
	data, err := json.MarshalIndent(s, "", "  ")
	if err != nil {
		return err
	}
	return privateWrite(filepath.Join(dir, "settings.json"), append(data, '\n'))
}

func loadSettings(dir string) (settings, error) {
	s := settings{ICEPort: 3478}
	data, err := os.ReadFile(filepath.Join(dir, "settings.json"))
	if errors.Is(err, os.ErrNotExist) {
		return s, saveSettings(dir, s)
	}
	if err != nil {
		return s, err
	}
	if err := json.Unmarshal(data, &s); err != nil {
		return s, errors.New("settings.json 无法解析")
	}
	return s, s.validate()
}

func prepareLayout(app, state string) error {
	homeBase := filepath.Join(state, "home", ".wxalh", "airan-desk")
	for _, dir := range []string{state, filepath.Join(state, "conf"), filepath.Join(state, "logs"), homeBase, filepath.Join(homeBase, "state")} {
		if err := os.MkdirAll(dir, 0700); err != nil {
			return err
		}
	}
	for _, entry := range []struct{ link, target string }{
		{filepath.Join(app, "airan", "conf"), filepath.Join(state, "conf")},
		{filepath.Join(app, "airan", "logs"), filepath.Join(state, "logs")},
		{filepath.Join(homeBase, "conf"), filepath.Join(state, "conf")},
	} {
		if fi, err := os.Lstat(entry.link); err == nil {
			if fi.Mode()&os.ModeSymlink == 0 {
				return fmt.Errorf("persistent link target exists and is not a symlink: %s", entry.link)
			}
			target, err := os.Readlink(entry.link)
			if err != nil || target != entry.target {
				return fmt.Errorf("unexpected persistent symlink: %s", entry.link)
			}
		} else if !errors.Is(err, os.ErrNotExist) {
			return err
		} else if err := os.Symlink(entry.target, entry.link); err != nil {
			return err
		}
	}
	defaults := filepath.Join(app, "airan", "defaults-conf")
	entries, err := os.ReadDir(defaults)
	if err != nil {
		return fmt.Errorf("missing complete portable defaults: %w", err)
	}
	for _, e := range entries {
		if !e.Type().IsRegular() || e.Name() == "id.ini" || e.Name() == "airan.ini" || e.Name() == "common.ini" {
			continue
		}
		dst := filepath.Join(state, "conf", e.Name())
		if _, err := os.Lstat(dst); err == nil {
			continue
		} else if !errors.Is(err, os.ErrNotExist) {
			return err
		}
		f, err := os.Open(filepath.Join(defaults, e.Name()))
		if err != nil {
			return err
		}
		data, err := io.ReadAll(io.LimitReader(f, 8<<20))
		f.Close()
		if err != nil {
			return err
		}
		if err = privateWrite(dst, data); err != nil {
			return err
		}
	}
	return nil
}

type identity struct{ ID, Password string }

func readIdentity(state string) (identity, error) {
	data, err := os.ReadFile(filepath.Join(state, "conf", "id.ini"))
	if err != nil {
		return identity{}, err
	}
	var id identity
	section := ""
	for _, line := range strings.Split(string(data), "\n") {
		line = strings.TrimSpace(line)
		if strings.HasPrefix(line, "[") && strings.HasSuffix(line, "]") {
			section = strings.Trim(line, "[]")
			continue
		}
		key, value, ok := strings.Cut(line, "=")
		if !ok || section != "local" {
			continue
		}
		value = strings.ToUpper(strings.Trim(strings.TrimSpace(value), "\"{}"))
		if key == "local_id" {
			id.ID = value
		}
		if key == "local_pwd" {
			id.Password = value
		}
	}
	if !validUUID(id.ID) || !validUUID(id.Password) {
		return id, errors.New("设备身份文件无效，未覆盖原文件")
	}
	return id, nil
}

func writeIdentity(state string, id identity) error {
	if !validUUID(id.ID) || !validUUID(id.Password) {
		return errors.New("设备码和连接密码须为有效且非全零的 UUID")
	}
	return privateWrite(filepath.Join(state, "conf", "id.ini"), []byte("[local]\nlocal_id="+id.ID+"\nlocal_pwd="+id.Password+"\n"))
}

func ensureIdentity(state string) error {
	_, err := readIdentity(state)
	if err == nil {
		return nil
	}
	if !errors.Is(err, os.ErrNotExist) {
		return err
	}
	id, err := uuid()
	if err != nil {
		return err
	}
	pass, err := uuid()
	if err != nil {
		return err
	}
	return writeIdentity(state, identity{id, pass})
}

func markerPath(state string) string {
	return filepath.Join(state, "home", ".wxalh", "airan-desk", "state", "owner-agreement-v1")
}
func accepted(state string) bool {
	data, err := os.ReadFile(markerPath(state))
	return err == nil && strings.TrimSpace(string(data)) == "owner-agreement-v1"
}
