package img

import (
	"crypto/sha1"
	"encoding/hex"
	"fmt"
	"image"
	"io"
	"net/http"
	"sync"
	"time"

	_ "image/gif"
	_ "image/jpeg"
	_ "image/png"
)

// Size es el lado del cuadrado que recibe el firmware. 64x64 en RGB565 son
// 8192 bytes, que el ESP32 pinta directo sin decodificar nada.
const Size = 64

const (
	maxDownload = 2 << 20
	fetchLimit  = 12 * time.Second
)

func Key(url string) string {
	sum := sha1.Sum([]byte(url))
	return hex.EncodeToString(sum[:])[:16]
}

// ToRGB565 decodifica, recorta al cuadrado central y promedia por area hasta
// Size x Size. Recortar en vez de escalar deforma menos: una miniatura 16:9
// aplastada a cuadrado se ve mal.
func ToRGB565(raw []byte) ([]byte, error) {
	src, _, err := image.Decode(newReader(raw))
	if err != nil {
		return nil, err
	}

	b := src.Bounds()
	side := b.Dx()
	if b.Dy() < side {
		side = b.Dy()
	}
	offX := b.Min.X + (b.Dx()-side)/2
	offY := b.Min.Y + (b.Dy()-side)/2

	out := make([]byte, Size*Size*2)
	for y := 0; y < Size; y++ {
		y0 := offY + y*side/Size
		y1 := offY + (y+1)*side/Size
		if y1 <= y0 {
			y1 = y0 + 1
		}
		for x := 0; x < Size; x++ {
			x0 := offX + x*side/Size
			x1 := offX + (x+1)*side/Size
			if x1 <= x0 {
				x1 = x0 + 1
			}

			var rs, gs, bs, n uint32
			for sy := y0; sy < y1; sy++ {
				for sx := x0; sx < x1; sx++ {
					r, g, bl, _ := src.At(sx, sy).RGBA()
					rs += r >> 8
					gs += g >> 8
					bs += bl >> 8
					n++
				}
			}
			if n == 0 {
				n = 1
			}
			r8 := uint16(rs / n)
			g8 := uint16(gs / n)
			b8 := uint16(bs / n)

			v := ((r8 & 0xF8) << 8) | ((g8 & 0xFC) << 3) | (b8 >> 3)
			i := (y*Size + x) * 2
			out[i] = byte(v)          // little-endian: el firmware lo lee asi
			out[i+1] = byte(v >> 8)
		}
	}
	return out, nil
}

type Cache struct {
	mu     sync.Mutex
	data   map[string][]byte
	source map[string]string // key -> URL, para poder re-resolver
	http   *http.Client
}

func NewCache() *Cache {
	return &Cache{
		data:   make(map[string][]byte),
		source: make(map[string]string),
		http:   &http.Client{Timeout: fetchLimit},
	}
}

// Get re-resuelve si conoce la URL pero perdio los pixeles. Sin esto, un
// reinicio del proxy deja al firmware pidiendo keys que dan 404 hasta el
// siguiente refresco del feed.
func (c *Cache) Get(key string) ([]byte, bool) {
	c.mu.Lock()
	raw, ok := c.data[key]
	url := c.source[key]
	c.mu.Unlock()

	if ok {
		return raw, true
	}
	if url == "" {
		return nil, false
	}
	if c.Resolve(url) == "" {
		return nil, false
	}

	c.mu.Lock()
	defer c.mu.Unlock()
	raw, ok = c.data[key]
	return raw, ok
}

// Resolve baja y convierte la imagen si no esta cacheada, y devuelve su key.
// Devuelve "" si no se pudo: una imagen faltante no debe romper el item.
func (c *Cache) Resolve(url string) string {
	if url == "" {
		return ""
	}
	key := Key(url)

	c.mu.Lock()
	c.source[key] = url
	_, have := c.data[key]
	c.mu.Unlock()
	if have {
		return key
	}

	resp, err := c.http.Get(url)
	if err != nil {
		return ""
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return ""
	}
	raw, err := io.ReadAll(io.LimitReader(resp.Body, maxDownload))
	if err != nil {
		return ""
	}
	pixels, err := ToRGB565(raw)
	if err != nil {
		return ""
	}

	c.mu.Lock()
	c.data[key] = pixels
	c.mu.Unlock()
	return key
}

func (c *Cache) Len() int {
	c.mu.Lock()
	defer c.mu.Unlock()
	return len(c.data)
}

func newReader(b []byte) *byteReader { return &byteReader{b: b} }

type byteReader struct {
	b []byte
	i int
}

func (r *byteReader) Read(p []byte) (int, error) {
	if r.i >= len(r.b) {
		return 0, io.EOF
	}
	n := copy(p, r.b[r.i:])
	r.i += n
	return n, nil
}

var _ = fmt.Sprintf
