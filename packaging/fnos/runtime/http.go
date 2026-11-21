package main

import (
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"embed"
	"encoding/base64"
	"errors"
	"html/template"
	"log"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"sync"
)

const settingsPath = "/app/airan-desk/"

//go:embed settings.html settings.css share.js
var assets embed.FS
var page = template.Must(template.ParseFS(assets, "settings.html"))

type manager struct {
	mu      sync.Mutex
	current settings
	child   childProcess
	csrfKey [32]byte
}

func newManager(s settings, app, state string) (*manager, error) {
	m := &manager{current: s, child: childProcess{app: app, state: state}}
	_, err := rand.Read(m.csrfKey[:])
	return m, err
}
func adminID(r *http.Request) string {
	a, u := r.Header.Values("X-Trim-Isadmin"), r.Header.Values("X-Trim-Userid")
	if len(a) != 1 || a[0] != "true" || len(u) != 1 || u[0] == "" {
		return ""
	}
	for _, c := range u[0] {
		if c < '0' || c > '9' {
			return ""
		}
	}
	n, err := strconv.ParseUint(u[0], 10, 64)
	if err != nil || n == 0 {
		return ""
	}
	return strconv.FormatUint(n, 10)
}
func (m *manager) token(id string) string {
	mac := hmac.New(sha256.New, m.csrfKey[:])
	_, _ = mac.Write([]byte(id))
	return base64.RawURLEncoding.EncodeToString(mac.Sum(nil))
}

type view struct {
	Settings                           settings
	CSRF, ProcessStatus, Notice, Error string
	Running, Accepted                  bool
	Identity                           identity
	Notifications                      []notification
	ShareText                          string
}

// Match MainWindow's zh_CN textToCopy translation; desktop paste accepts this
// ID and verification code format and deliberately does not import server secrets.
func desktopShareText(id identity) string {
	if !validUUID(id.ID) || !validUUID(id.Password) {
		return ""
	}
	return "欢迎使用 airan 远程工具。你的 ID：" + id.ID + "\n验证码：" + id.Password
}

func (m *manager) show(w http.ResponseWriter, token string, status int, message string) {
	id, err := readIdentity(m.child.state)
	data := view{Settings: m.current, CSRF: token, ProcessStatus: m.child.status(), Running: m.child.running(),
		Accepted: accepted(m.child.state), Identity: id, Notifications: recentNotifications(m.child.state),
		ShareText: desktopShareText(id)}
	if err != nil {
		data.Error = err.Error()
	}
	if status == http.StatusOK {
		data.Notice = message
	} else {
		data.Error = message
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.WriteHeader(status)
	if err := page.ExecuteTemplate(w, "settings.html", data); err != nil {
		log.Printf("render settings: %v", err)
	}
}
func fromForm(f url.Values, old settings) (settings, error) {
	for _, key := range []string{"ws_url", "ice_host", "ice_port", "ice_username", "ice_password"} {
		if len(f[key]) != 1 {
			return old, errors.New("表单字段缺失或重复")
		}
	}
	for _, key := range []string{"allow_remote", "owner_accept"} {
		if len(f[key]) > 1 {
			return old, errors.New("表单字段重复")
		}
	}
	port, err := strconv.Atoi(f.Get("ice_port"))
	if err != nil {
		return old, errors.New("STUN / TURN 端口无效")
	}
	next := settings{AllowRemote: f.Get("allow_remote") == "on", WSServer: strings.TrimSpace(f.Get("ws_url")),
		ICEHost: strings.TrimSpace(f.Get("ice_host")), ICEPort: port, ICEUser: f.Get("ice_username"), ICEPassword: old.ICEPassword}
	if f.Get("ice_password") != "" {
		next.ICEPassword = f.Get("ice_password")
	}
	return next, next.validate()
}
func (m *manager) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Cache-Control", "no-store")
	w.Header().Set("X-Content-Type-Options", "nosniff")
	w.Header().Set("Referrer-Policy", "no-referrer")
	w.Header().Set("Content-Security-Policy", "default-src 'none'; style-src 'self'; script-src 'self'; form-action 'self'; frame-ancestors 'self'; base-uri 'none'")
	if r.URL.Path == "/_health" && r.Method == http.MethodGet {
		_, _ = w.Write([]byte("ok\n"))
		return
	}
	id := adminID(r)
	if id == "" {
		http.Error(w, "仅飞牛管理员可访问此设置", http.StatusForbidden)
		return
	}
	if r.URL.Path == strings.TrimSuffix(settingsPath, "/") && r.Method == http.MethodGet {
		http.Redirect(w, r, settingsPath, http.StatusPermanentRedirect)
		return
	}
	if r.URL.Path == settingsPath+"settings.css" && r.Method == http.MethodGet {
		css, _ := assets.ReadFile("settings.css")
		w.Header().Set("Content-Type", "text/css; charset=utf-8")
		_, _ = w.Write(css)
		return
	}
	if r.URL.Path == settingsPath+"share.js" && r.Method == http.MethodGet {
		js, _ := assets.ReadFile("share.js")
		w.Header().Set("Content-Type", "text/javascript; charset=utf-8")
		_, _ = w.Write(js)
		return
	}
	if r.URL.Path != settingsPath {
		http.NotFound(w, r)
		return
	}
	token := m.token(id)
	switch r.Method {
	case http.MethodGet:
		m.mu.Lock()
		defer m.mu.Unlock()
		message := ""
		if r.URL.Query().Get("saved") == "1" {
			message = "设置已保存"
		}
		if r.URL.Query().Get("reset") == "1" {
			message = "连接密码（验证码）已更换，设备 ID 保持不变"
		}
		m.show(w, token, http.StatusOK, message)
	case http.MethodPost:
		r.Body = http.MaxBytesReader(w, r.Body, 16<<10)
		if err := r.ParseForm(); err != nil || len(r.PostForm["action"]) != 1 {
			http.Error(w, "无效请求", http.StatusBadRequest)
			return
		}
		if len(r.PostForm["csrf"]) != 1 || !hmac.Equal([]byte(r.PostForm.Get("csrf")), []byte(token)) {
			http.Error(w, "页面已过期，请刷新后重试", http.StatusForbidden)
			return
		}
		m.mu.Lock()
		defer m.mu.Unlock()
		switch r.PostForm.Get("action") {
		case "save":
			next, err := fromForm(r.PostForm, m.current)
			if err != nil {
				m.show(w, token, http.StatusBadRequest, err.Error())
				return
			}
			if next.AllowRemote && !accepted(m.child.state) {
				if r.PostForm.Get("owner_accept") != "on" {
					m.show(w, token, http.StatusBadRequest, "请先确认设备所有者授权")
					return
				}
				if err = privateWrite(markerPath(m.child.state), []byte("owner-agreement-v1\n")); err != nil {
					log.Printf("persist agreement: %v", err)
					m.show(w, token, http.StatusInternalServerError, "无法保存授权")
					return
				}
			}
			if err = m.child.apply(m.current, next); err != nil {
				m.show(w, token, http.StatusConflict, err.Error())
				return
			}
			m.current = next
			http.Redirect(w, r, settingsPath+"?saved=1", http.StatusSeeOther)
		case "reset_password":
			if err := m.child.resetPassword(m.current); err != nil {
				m.show(w, token, http.StatusConflict, err.Error())
				return
			}
			http.Redirect(w, r, settingsPath+"?reset=1", http.StatusSeeOther)
		default:
			http.Error(w, "未知操作", http.StatusBadRequest)
		}
	default:
		w.Header().Set("Allow", "GET, POST")
		http.Error(w, "不支持此请求方法", http.StatusMethodNotAllowed)
	}
}
