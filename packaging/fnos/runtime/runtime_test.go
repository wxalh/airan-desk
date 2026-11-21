package main

import (
	"encoding/json"
	"net/http/httptest"
	"net/url"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"
)

func TestConfigRejectsInjectionAndInvalidServers(t *testing.T) {
	s := settings{WSServer: "ws://127.0.0.1:3480/", ICEPort: 3478, AllowRemote: true}
	if err := s.validate(); err != nil {
		t.Fatal(err)
	}
	for _, bad := range []string{"https://example.com", "ws://", "ws://host/\n[local]", "ws://user:password@host"} {
		s.WSServer = bad
		if s.validate() == nil {
			t.Fatalf("accepted %q", bad)
		}
	}
	s.WSServer = ""
	if s.validate() == nil {
		t.Fatal("enabled without signal server")
	}
	s.AllowRemote = false
	if err := s.validate(); err != nil {
		t.Fatal(err)
	}
}

func TestSettingsAuthorizationRequiresUniqueAdminHeaders(t *testing.T) {
	r := httptest.NewRequest("GET", settingsPath, nil)
	r.Header.Set("X-Trim-Userid", "1001")
	if adminID(r) != "" {
		t.Fatal("accepted nonadmin")
	}
	r.Header.Set("X-Trim-Isadmin", "true")
	if adminID(r) != "1001" {
		t.Fatal("rejected administrator")
	}
	r.Header.Add("X-Trim-Isadmin", "false")
	if adminID(r) != "" {
		t.Fatal("accepted duplicate header")
	}
}

func TestSharingMatchesDesktopTextAndKeepsScriptAuthenticated(t *testing.T) {
	id := identity{"01234567-89AB-4CDE-8F01-23456789ABCD", "ABCDEF01-2345-4678-9ABC-DEF012345678"}
	want := "欢迎使用 airan 远程工具。你的 ID：" + id.ID + "\n验证码：" + id.Password
	if got := desktopShareText(id); got != want {
		t.Fatal("share text differs from desktop format")
	}
	m, err := newManager(settings{ICEPort: 3478}, t.TempDir(), t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	r := httptest.NewRequest("GET", settingsPath+"share.js", nil)
	w := httptest.NewRecorder()
	m.ServeHTTP(w, r)
	if w.Code != 403 {
		t.Fatal("script bypassed gateway authorization")
	}
	r.Header.Set("X-Trim-Isadmin", "true")
	r.Header.Set("X-Trim-Userid", "1001")
	w = httptest.NewRecorder()
	m.ServeHTTP(w, r)
	if w.Code != 200 || w.Header().Get("Content-Type") != "text/javascript; charset=utf-8" {
		t.Fatal("share script unavailable")
	}
	csp := w.Header().Get("Content-Security-Policy")
	if !strings.Contains(csp, "script-src 'self'") || strings.Contains(csp, "unsafe-inline") {
		t.Fatal("share script CSP is not restricted")
	}
}

func TestLayoutPersistsIdentityAndNeverClobbersUnrelatedDirectories(t *testing.T) {
	app, state := t.TempDir(), t.TempDir()
	if err := os.MkdirAll(filepath.Join(app, "airan", "defaults-conf"), 0700); err != nil {
		t.Fatal(err)
	}
	if err := prepareLayout(app, state); err != nil {
		t.Fatal(err)
	}
	if err := ensureIdentity(state); err != nil {
		t.Fatal(err)
	}
	before, err := os.ReadFile(filepath.Join(state, "conf", "id.ini"))
	if err != nil {
		t.Fatal(err)
	}
	if err := ensureIdentity(state); err != nil {
		t.Fatal(err)
	}
	after, _ := os.ReadFile(filepath.Join(state, "conf", "id.ini"))
	if string(before) != string(after) || !strings.Contains(string(after), "local_pwd=") {
		t.Fatal("identity changed")
	}
	real, err := filepath.EvalSymlinks(filepath.Join(app, "airan", "conf"))
	if err != nil || real != filepath.Join(state, "conf") {
		t.Fatal("conf is not persistent", real, err)
	}
	if accepted(state) {
		t.Fatal("layout accepted owner agreement without explicit action")
	}
}

func TestIdentityRejectsInvalidUUIDWithoutChangingStoredIdentity(t *testing.T) {
	state := t.TempDir()
	if err := os.MkdirAll(filepath.Join(state, "conf"), 0700); err != nil {
		t.Fatal(err)
	}
	valid := identity{"01234567-89AB-4CDE-8F01-23456789ABCD", "ABCDEF01-2345-4678-9ABC-DEF012345678"}
	if err := writeIdentity(state, valid); err != nil {
		t.Fatal(err)
	}
	path := filepath.Join(state, "conf", "id.ini")
	before, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	for _, bad := range []string{"", "custom-password", "00000000-0000-0000-0000-000000000000", "0123456789AB4CDE8F0123456789ABCD"} {
		for _, field := range []string{"id", "password"} {
			t.Run(field+"/"+bad, func(t *testing.T) {
				if err := writeIdentity(state, valid); err != nil {
					t.Fatal(err)
				}
				invalid := valid
				if field == "id" {
					invalid.ID = bad
				} else {
					invalid.Password = bad
				}
				if err := writeIdentity(state, invalid); err == nil {
					t.Error("accepted invalid identity")
				}
				after, err := os.ReadFile(path)
				if err != nil || string(after) != string(before) {
					t.Fatal("invalid update changed stored identity", err)
				}
				if desktopShareText(invalid) != "" {
					t.Error("shared unusable identity")
				}
			})
		}
	}
}

func TestManuallyEditedInvalidIdentityIsPreservedAndRejected(t *testing.T) {
	for _, field := range []string{"local_id", "local_pwd"} {
		t.Run(field, func(t *testing.T) {
			state := t.TempDir()
			if err := os.MkdirAll(filepath.Join(state, "conf"), 0700); err != nil {
				t.Fatal(err)
			}
			data := "[local]\nlocal_id=01234567-89AB-4CDE-8F01-23456789ABCD\nlocal_pwd=ABCDEF01-2345-4678-9ABC-DEF012345678\n"
			if field == "local_id" {
				data = strings.Replace(data, "01234567-89AB-4CDE-8F01-23456789ABCD", "00000000-0000-0000-0000-000000000000", 1)
			} else {
				data = strings.Replace(data, "ABCDEF01-2345-4678-9ABC-DEF012345678", "00000000-0000-0000-0000-000000000000", 1)
			}
			path := filepath.Join(state, "conf", "id.ini")
			if err := os.WriteFile(path, []byte(data), 0600); err != nil {
				t.Fatal(err)
			}
			if _, err := readIdentity(state); err == nil {
				t.Error("accepted a manually edited null UUID")
			}
			if ensureIdentity(state) == nil {
				t.Error("accepted invalid existing identity at startup")
			}
			after, err := os.ReadFile(path)
			if err != nil || string(after) != data {
				t.Fatal("invalid existing identity was overwritten", err)
			}
		})
	}
}

func TestPasswordButtonKeepsIDAndUpdatesSharedUUID(t *testing.T) {
	app, state := t.TempDir(), t.TempDir()
	if err := os.MkdirAll(filepath.Join(app, "airan", "defaults-conf"), 0700); err != nil {
		t.Fatal(err)
	}
	if err := prepareLayout(app, state); err != nil {
		t.Fatal(err)
	}
	old := identity{"01234567-89AB-4CDE-8F01-23456789ABCD", "ABCDEF01-2345-4678-9ABC-DEF012345678"}
	if err := writeIdentity(state, old); err != nil {
		t.Fatal(err)
	}
	m, err := newManager(settings{ICEPort: 3478}, app, state)
	if err != nil {
		t.Fatal(err)
	}
	f := url.Values{"action": {"reset_password"}, "csrf": {m.token("1001")}}
	r := httptest.NewRequest("POST", settingsPath, strings.NewReader(f.Encode()))
	r.Header.Set("X-Trim-Isadmin", "true")
	r.Header.Set("X-Trim-Userid", "1001")
	r.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	w := httptest.NewRecorder()
	m.ServeHTTP(w, r)
	if w.Code != 303 {
		t.Fatal("password reset failed", w.Code)
	}
	next, err := readIdentity(state)
	if err != nil {
		t.Fatal(err)
	}
	if next.ID != old.ID || next.Password == old.Password {
		t.Fatal("reset changed ID or kept old password")
	}
	if !regexp.MustCompile(`^[0-9A-F]{8}-[0-9A-F]{4}-4[0-9A-F]{3}-[89AB][0-9A-F]{3}-[0-9A-F]{12}$`).MatchString(next.Password) {
		t.Fatal("reset did not generate a version 4 UUID")
	}
	r = httptest.NewRequest("GET", settingsPath+"?reset=1", nil)
	r.Header.Set("X-Trim-Isadmin", "true")
	r.Header.Set("X-Trim-Userid", "1001")
	w = httptest.NewRecorder()
	m.ServeHTTP(w, r)
	wantShare := "欢迎使用 airan 远程工具。你的 ID：" + old.ID + "\n验证码：" + next.Password
	if w.Code != 200 || !strings.Contains(w.Body.String(), wantShare) || strings.Contains(w.Body.String(), old.Password) {
		t.Fatal("page did not share the updated identity")
	}
	if err := ensureIdentity(state); err != nil {
		t.Fatal(err)
	}
	persisted, err := readIdentity(state)
	if err != nil || persisted != next {
		t.Fatal("new password did not persist", err)
	}
}

func TestQSettingsQuotingAndNotification(t *testing.T) {
	if iniString("@x,y\nz") != "\"@@x,y\\nz\"" {
		t.Fatal(iniString("@x,y\nz"))
	}
	dir := t.TempDir()
	if err := recordNotification(dir, "connection_requested", "peer-id", "terminal"); err != nil {
		t.Fatal(err)
	}
	data, err := os.ReadFile(filepath.Join(dir, "notifications.jsonl"))
	if err != nil || !strings.Contains(string(data), "connection_requested") {
		t.Fatal(err)
	}
	if recordNotification(dir, "arbitrary", "peer-id", "x") == nil {
		t.Fatal("accepted arbitrary notification event")
	}
}

func TestAdminSaveRequiresConsentAndCSRF(t *testing.T) {
	app, state := t.TempDir(), t.TempDir()
	os.MkdirAll(filepath.Join(app, "airan", "defaults-conf"), 0700)
	if err := prepareLayout(app, state); err != nil {
		t.Fatal(err)
	}
	if err := ensureIdentity(state); err != nil {
		t.Fatal(err)
	}
	m, err := newManager(settings{ICEPort: 3478}, app, state)
	if err != nil {
		t.Fatal(err)
	}
	request := func(method string, f url.Values) *httptest.ResponseRecorder {
		r := httptest.NewRequest(method, settingsPath, strings.NewReader(f.Encode()))
		r.Header.Set("X-Trim-Isadmin", "true")
		r.Header.Set("X-Trim-Userid", "1001")
		if method == "POST" {
			r.Header.Set("Content-Type", "application/x-www-form-urlencoded")
		}
		w := httptest.NewRecorder()
		m.ServeHTTP(w, r)
		return w
	}
	w := request("GET", nil)
	if w.Code != 200 {
		t.Fatal(w.Code)
	}
	match := regexp.MustCompile("name=\"csrf\" value=\"([^\"]+)\"").FindStringSubmatch(w.Body.String())
	if len(match) != 2 {
		t.Fatal("missing CSRF")
	}
	f := url.Values{"action": {"save"}, "csrf": {match[1]}, "ws_url": {"ws://127.0.0.1:3480/"},
		"ice_host": {""}, "ice_port": {"3478"}, "ice_username": {""}, "ice_password": {""}, "allow_remote": {"on"}}
	if request("POST", f).Code != 400 || accepted(state) {
		t.Fatal("enabled without consent")
	}
	f.Set("owner_accept", "on")
	f.Set("csrf", "invalid")
	if request("POST", f).Code != 403 || accepted(state) {
		t.Fatal("accepted invalid CSRF")
	}
	f.Set("csrf", match[1])
	f.Del("allow_remote")
	f.Del("owner_accept")
	if request("POST", f).Code != 303 || accepted(state) {
		t.Fatal("disabled save created consent")
	}
}

func TestChildFailureRollsBackConfiguration(t *testing.T) {
	app, state := t.TempDir(), t.TempDir()
	os.MkdirAll(filepath.Join(app, "airan", "defaults-conf"), 0700)
	if err := prepareLayout(app, state); err != nil {
		t.Fatal(err)
	}
	if err := privateWrite(markerPath(state), []byte("owner-agreement-v1\n")); err != nil {
		t.Fatal(err)
	}
	// The fixed test wrapper also verifies the effective no-display environment.
	script := "#!/bin/sh\nset -eu\n" +
		"test -z \"${DISPLAY:-}\"\n" +
		"test -z \"${WAYLAND_DISPLAY:-}\"\n" +
		"test \"$AIRAN_DESK_SKIP_UINPUT_SETUP\" = 1\n" +
		"test \"$HOME\" = \"$AIRAN_DESK_STATE_DIR/home\"\n" +
		"if grep -q 'invalid.example' conf/common.ini; then exit 41; fi\n" +
		"exec sleep 60\n"
	if err := os.WriteFile(filepath.Join(app, "airan", "airan-desk"), []byte(script), 0700); err != nil {
		t.Fatal(err)
	}
	t.Setenv("DISPLAY", ":fake")
	t.Setenv("WAYLAND_DISPLAY", "wayland-fake")
	old := settings{AllowRemote: true, WSServer: "ws://localhost:3480", ICEPort: 3478}
	p := childProcess{app: app, state: state}
	defer p.stop()
	if err := p.start(old); err != nil {
		t.Fatal(err)
	}
	next := old
	next.WSServer = "ws://invalid.example:3480"
	if p.apply(old, next) == nil {
		t.Fatal("accepted failed child")
	}
	if !p.running() {
		t.Fatal("old child not restored")
	}
	data, err := os.ReadFile(filepath.Join(state, "settings.json"))
	if err != nil {
		t.Fatal(err)
	}
	var persisted settings
	if err := json.Unmarshal(data, &persisted); err != nil || persisted != old {
		t.Fatal("old settings not restored", err)
	}
	content, err := os.ReadFile(filepath.Join(state, "conf", "common.ini"))
	if err != nil || strings.Contains(string(content), "invalid.example") {
		t.Fatal("config not restored")
	}
}
