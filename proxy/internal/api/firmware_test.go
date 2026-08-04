package api

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/mixer"
)

func dirConBinario(t *testing.T, contenido string) string {
	t.Helper()
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "firmware.bin"), []byte(contenido), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "version.txt"), []byte("1.2.3\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	return dir
}

func TestFirmwareInformaVersionTamanoYHash(t *testing.T) {
	fw := &Firmware{Dir: dirConBinario(t, "hola")}
	rec := httptest.NewRecorder()
	fw.serveMeta(rec, httptest.NewRequest(http.MethodGet, "/v1/firmware", nil))

	if rec.Code != http.StatusOK {
		t.Fatalf("status = %d", rec.Code)
	}
	var m struct {
		Version string `json:"version"`
		Size    int64  `json:"size"`
		SHA256  string `json:"sha256"`
	}
	if err := json.Unmarshal(rec.Body.Bytes(), &m); err != nil {
		t.Fatalf("respuesta no es JSON: %v", err)
	}
	if m.Version != "1.2.3" {
		t.Errorf("version = %q", m.Version)
	}
	if m.Size != 4 {
		t.Errorf("size = %d, quiero 4", m.Size)
	}
	// sha256 de "hola"
	const want = "b221d9dbb083a7f33428d7c2a3c3198ae925614d70210e28716ccaa7cd4ddb79"
	if m.SHA256 != want {
		t.Errorf("sha256 = %q", m.SHA256)
	}
}

func TestFirmwareSirveElBinarioConLargoDeclarado(t *testing.T) {
	fw := &Firmware{Dir: dirConBinario(t, "hola")}
	rec := httptest.NewRecorder()
	fw.serveBin(rec, httptest.NewRequest(http.MethodGet, "/v1/firmware/bin", nil))

	if rec.Code != http.StatusOK {
		t.Fatalf("status = %d", rec.Code)
	}
	if rec.Body.String() != "hola" {
		t.Errorf("cuerpo = %q", rec.Body.String())
	}
	// Sin Content-Length el HTTPClient del ESP32 recibe chunked y Update.h no
	// sabe cuanto espera. Es la misma trampa que ya mordio al feed.
	if rec.Header().Get("Content-Length") == "" {
		t.Error("falta Content-Length")
	}
}

func TestFirmwareSin404CuandoNoHayBinario(t *testing.T) {
	fw := &Firmware{Dir: t.TempDir()}
	rec := httptest.NewRecorder()
	fw.serveMeta(rec, httptest.NewRequest(http.MethodGet, "/v1/firmware", nil))
	if rec.Code != http.StatusNotFound {
		t.Errorf("status = %d, quiero 404", rec.Code)
	}
}

func TestFirmwareDeshabilitadoSinDir(t *testing.T) {
	fw := &Firmware{Dir: ""}
	rec := httptest.NewRecorder()
	fw.serveMeta(rec, httptest.NewRequest(http.MethodGet, "/v1/firmware", nil))
	if rec.Code != http.StatusNotFound {
		t.Errorf("sin FIRMWARE_DIR tiene que dar 404, dio %d", rec.Code)
	}
}

func hashDe(t *testing.T, fw *Firmware) string {
	t.Helper()
	rec := httptest.NewRecorder()
	fw.serveMeta(rec, httptest.NewRequest(http.MethodGet, "/v1/firmware", nil))
	if rec.Code != http.StatusOK {
		t.Fatalf("status = %d", rec.Code)
	}
	var m struct {
		SHA256 string `json:"sha256"`
	}
	if err := json.Unmarshal(rec.Body.Bytes(), &m); err != nil {
		t.Fatal(err)
	}
	return m.SHA256
}

// El hash se recuerda por mtime y tamano. Un cache que no se invalida es peor
// que no cachear: el aparato bajaria el binario nuevo, lo compararia contra el
// hash viejo y abortaria el OTA para siempre.
func TestFirmwareRecuerdaElHashHastaQueElBinarioCambia(t *testing.T) {
	dir := dirConBinario(t, "hola")
	bin := filepath.Join(dir, "firmware.bin")
	fw := &Firmware{Dir: dir}

	primero := hashDe(t, fw)

	// Mismo tamano y mismo mtime: el hash sale del cache sin tocar el disco.
	// Es la unica forma de observar que el cache existe, y de paso deja
	// anotado su limite: reescribir el binario conservando la fecha lo enganha.
	st, err := os.Stat(bin)
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(bin, []byte("chau"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.Chtimes(bin, st.ModTime(), st.ModTime()); err != nil {
		t.Fatal(err)
	}
	if cacheado := hashDe(t, fw); cacheado != primero {
		t.Errorf("recalculo el hash con el archivo sin cambios visibles: %q", cacheado)
	}

	// Un binario de otro tamano si tiene que notarse.
	if err := os.WriteFile(bin, []byte("hola de nuevo"), 0o644); err != nil {
		t.Fatal(err)
	}
	if nuevo := hashDe(t, fw); nuevo == primero {
		t.Fatal("el cache quedo pegado a un binario que ya no esta")
	}
}

// El guard tiene que cubrir tambien al OTA: descargar un ejecutable no puede
// ser mas facil que leer un titular. El pedido con token al final esta para que
// el 401 no pueda venir de una ruta que en realidad no existe.
func TestFirmwarePideTokenComoElResto(t *testing.T) {
	h := New(mixer.New(time.Minute, fake{}))
	h.SetGuard(guarded("secreto", 100, 100))
	h.SetFirmware(&Firmware{Dir: dirConBinario(t, "hola")})

	for _, ruta := range []string{"/v1/firmware", "/v1/firmware/bin"} {
		rec := httptest.NewRecorder()
		h.ServeHTTP(rec, req(http.MethodGet, ruta, ""))
		if rec.Code != http.StatusUnauthorized {
			t.Errorf("%s sin token: status = %d, quiero 401", ruta, rec.Code)
		}
	}

	rec := httptest.NewRecorder()
	h.ServeHTTP(rec, req(http.MethodGet, "/v1/firmware", "Bearer secreto"))
	if rec.Code != http.StatusOK {
		t.Errorf("con token: status = %d, quiero 200", rec.Code)
	}
}

// Sin SetFirmware las rutas existen pero no sirven nada: encender el OTA es una
// decision explicita del que arranca el proxy.
func TestFirmwareApagadoDaNotFound(t *testing.T) {
	h := New(mixer.New(time.Minute, fake{}))
	for _, ruta := range []string{"/v1/firmware", "/v1/firmware/bin"} {
		rec := httptest.NewRecorder()
		h.ServeHTTP(rec, httptest.NewRequest(http.MethodGet, ruta, nil))
		if rec.Code != http.StatusNotFound {
			t.Errorf("%s con el OTA apagado: status = %d, quiero 404", ruta, rec.Code)
		}
	}
}
