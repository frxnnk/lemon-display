package api

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
)

const (
	binNombre     = "firmware.bin"
	versionNombre = "version.txt"

	// Un numero de version no pasa de una linea. Leer acotado evita que un
	// archivo equivocado en la carpeta se convierta en la respuesta.
	versionMax = 64
)

// errSinFirmware junta todos los casos de "no hay nada que ofrecer": falta el
// binario, falta version.txt o esta vacio. Todos terminan en 404, que es lo
// que el aparato tiene que entender.
var errSinFirmware = errors.New("no hay firmware para servir")

// Firmware sirve el binario que el aparato baja por OTA. Dir es la carpeta del
// VPS donde se sube por scp, con firmware.bin y version.txt adentro. Con Dir
// vacio la funcion queda apagada y los dos endpoints dan 404: habilitar el OTA
// tiene que ser una decision explicita.
type Firmware struct {
	Dir string

	// El sha256 se recuerda entre pedidos. Recalcularlo cada vez es leer ~1 MB
	// de disco por request, y el aparato consulta la version antes de cada
	// actualizacion.
	mu    sync.Mutex
	sello sello
	hash  string
}

// sello identifica una version del binario en disco. mtime + tamano alcanza
// para notar uno nuevo subido por scp, que es la unica forma en que cambia.
// Se guarda el mtime en nanos y no como time.Time para poder comparar con ==.
type sello struct {
	modNano int64
	size    int64
}

type fwMeta struct {
	Version string `json:"version"`
	Size    int64  `json:"size"`
	SHA256  string `json:"sha256"`
}

func (f *Firmware) apagado() bool { return f == nil || f.Dir == "" }

// serveMeta responde version, tamano y hash del binario. El aparato lo compara
// con su propia FERCED_VERSION antes de decidir si baja algo.
func (f *Firmware) serveMeta(w http.ResponseWriter, r *http.Request) {
	if f.apagado() {
		http.NotFound(w, r)
		return
	}

	m, err := f.meta()
	if err != nil {
		if errors.Is(err, errSinFirmware) {
			log.Printf("[fw] %s pidio la version y no hay firmware en %s: %v",
				clientIP(r), f.Dir, err)
			http.NotFound(w, r)
			return
		}
		// Un permiso mal puesto o un disco con problemas no es un 404: si el
		// aparato lee "no hay nada" nadie se entera de que el OTA esta roto.
		log.Printf("[fw] no puedo leer %s: %v", f.Dir, err)
		http.Error(w, "no se pudo leer el firmware", http.StatusInternalServerError)
		return
	}

	body, err := json.Marshal(m)
	if err != nil {
		http.Error(w, "no se pudo serializar la metadata", http.StatusInternalServerError)
		return
	}

	// El user agent va aca a proposito: el aparato y la PC de casa salen por la
	// misma IP publica, y sin este dato no se distingue un pedido del aparato de
	// uno hecho a mano con curl. El 2026-08-05 eso arruino un diagnostico.
	log.Printf("[fw] %s pidio la version -> %s, %d bytes (%s)",
		clientIP(r), m.Version, m.Size, r.UserAgent())

	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Content-Length", strconv.Itoa(len(body)))
	// El aparato tiene que ver el binario que esta hoy en el VPS, no uno que
	// quedo cacheado en el camino.
	w.Header().Set("Cache-Control", "no-store")
	w.Write(body)
}

// serveBin entrega el .bin crudo. No se carga en memoria: son ~1 MB y el
// proxy corre al lado del feed.
func (f *Firmware) serveBin(w http.ResponseWriter, r *http.Request) {
	if f.apagado() {
		http.NotFound(w, r)
		return
	}

	bin, err := os.Open(filepath.Join(f.Dir, binNombre))
	if err != nil {
		if errors.Is(err, fs.ErrNotExist) {
			http.NotFound(w, r)
			return
		}
		log.Printf("[fw] no puedo abrir el binario: %v", err)
		http.Error(w, "no se pudo abrir el firmware", http.StatusInternalServerError)
		return
	}
	defer bin.Close()

	st, err := bin.Stat()
	if err != nil {
		log.Printf("[fw] no puedo medir el binario: %v", err)
		http.Error(w, "no se pudo leer el firmware", http.StatusInternalServerError)
		return
	}

	log.Printf("[fw] %s baja el binario, %d bytes (%s)", clientIP(r), st.Size(), r.UserAgent())

	// Content-Type puesto antes: con la cabecera ya presente ServeContent no
	// olfatea el contenido para adivinarla.
	w.Header().Set("Content-Type", "application/octet-stream")
	w.Header().Set("Cache-Control", "no-store")
	// ServeContent declara Content-Length y copia en streaming. Sin largo
	// declarado Go pasa a Transfer-Encoding: chunked, el HTTPClient del ESP32
	// entrega ese stream con el framing de chunks adentro y Update.h ademas se
	// queda sin saber cuantos bytes espera. Es la trampa que ya mordio al feed.
	http.ServeContent(w, r, binNombre, st.ModTime(), bin)
}

func (f *Firmware) meta() (fwMeta, error) {
	ruta := filepath.Join(f.Dir, binNombre)
	st, err := os.Stat(ruta)
	if err != nil {
		if errors.Is(err, fs.ErrNotExist) {
			return fwMeta{}, fmt.Errorf("%w: falta %s", errSinFirmware, binNombre)
		}
		return fwMeta{}, err
	}

	version, err := leerVersion(filepath.Join(f.Dir, versionNombre))
	if err != nil {
		return fwMeta{}, err
	}

	suma, err := f.sha256(ruta, sello{modNano: st.ModTime().UnixNano(), size: st.Size()})
	if err != nil {
		return fwMeta{}, err
	}

	return fwMeta{Version: version, Size: st.Size(), SHA256: suma}, nil
}

// sha256 devuelve el hash del binario, recalculandolo solo cuando el archivo
// cambio. El lock se sostiene durante el calculo a proposito: son unos pocos
// ms sobre un endpoint que se toca a mano, y asi dos pedidos simultaneos no
// pueden dejar cacheado el hash de un binario viejo.
func (f *Firmware) sha256(ruta string, s sello) (string, error) {
	f.mu.Lock()
	defer f.mu.Unlock()

	if f.hash != "" && f.sello == s {
		return f.hash, nil
	}

	h, err := hashDeArchivo(ruta)
	if err != nil {
		return "", err
	}
	f.sello, f.hash = s, h
	return h, nil
}

func hashDeArchivo(ruta string) (string, error) {
	f, err := os.Open(ruta)
	if err != nil {
		if errors.Is(err, fs.ErrNotExist) {
			return "", fmt.Errorf("%w: falta %s", errSinFirmware, binNombre)
		}
		return "", err
	}
	defer f.Close()

	h := sha256.New()
	if _, err := io.Copy(h, f); err != nil {
		return "", err
	}
	return hex.EncodeToString(h.Sum(nil)), nil
}

func leerVersion(ruta string) (string, error) {
	f, err := os.Open(ruta)
	if err != nil {
		if errors.Is(err, fs.ErrNotExist) {
			return "", fmt.Errorf("%w: falta %s", errSinFirmware, versionNombre)
		}
		return "", err
	}
	defer f.Close()

	buf := make([]byte, versionMax)
	n, err := f.Read(buf)
	if err != nil && !errors.Is(err, io.EOF) {
		return "", err
	}
	version := strings.TrimSpace(string(buf[:n]))
	if version == "" {
		return "", fmt.Errorf("%w: %s esta vacio", errSinFirmware, versionNombre)
	}
	return version, nil
}
