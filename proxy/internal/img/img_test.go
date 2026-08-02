package img

import (
	"bytes"
	"image"
	"image/color"
	"image/jpeg"
	"image/png"
	"testing"
)

func makePNG(t *testing.T, w, h int, c color.RGBA) []byte {
	t.Helper()
	m := image.NewRGBA(image.Rect(0, 0, w, h))
	for y := 0; y < h; y++ {
		for x := 0; x < w; x++ {
			m.Set(x, y, c)
		}
	}
	var buf bytes.Buffer
	if err := png.Encode(&buf, m); err != nil {
		t.Fatal(err)
	}
	return buf.Bytes()
}

func makeJPEG(t *testing.T, w, h int, c color.RGBA) []byte {
	t.Helper()
	m := image.NewRGBA(image.Rect(0, 0, w, h))
	for y := 0; y < h; y++ {
		for x := 0; x < w; x++ {
			m.Set(x, y, c)
		}
	}
	var buf bytes.Buffer
	if err := jpeg.Encode(&buf, m, nil); err != nil {
		t.Fatal(err)
	}
	return buf.Bytes()
}

func TestDecodeProducesExactByteCount(t *testing.T) {
	raw, err := ToRGB565(makePNG(t, 240, 135, color.RGBA{255, 0, 0, 255}))
	if err != nil {
		t.Fatal(err)
	}
	want := Size * Size * 2
	if len(raw) != want {
		t.Fatalf("bytes = %d, quiero %d (%dx%d RGB565)", len(raw), want, Size, Size)
	}
}

func TestAcceptsJPEG(t *testing.T) {
	if _, err := ToRGB565(makeJPEG(t, 400, 400, color.RGBA{0, 0, 255, 255})); err != nil {
		t.Fatalf("no acepto JPEG: %v", err)
	}
}

func TestColorSurvivesRoundTrip(t *testing.T) {
	raw, err := ToRGB565(makePNG(t, 128, 128, color.RGBA{255, 0, 0, 255}))
	if err != nil {
		t.Fatal(err)
	}
	// RGB565 little-endian: rojo puro = 0xF800.
	got := uint16(raw[0]) | uint16(raw[1])<<8
	if got != 0xF800 {
		t.Errorf("primer pixel = 0x%04X, quiero 0xF800 (rojo)", got)
	}
}

func TestNonSquareIsCroppedNotSquashed(t *testing.T) {
	// Mitad izquierda roja, derecha azul, en 200x100. Al recortar al centro
	// cuadrado (100x100) debe quedar la union: mitad roja, mitad azul.
	m := image.NewRGBA(image.Rect(0, 0, 200, 100))
	for y := 0; y < 100; y++ {
		for x := 0; x < 200; x++ {
			if x < 100 {
				m.Set(x, y, color.RGBA{255, 0, 0, 255})
			} else {
				m.Set(x, y, color.RGBA{0, 0, 255, 255})
			}
		}
	}
	var buf bytes.Buffer
	if err := png.Encode(&buf, m); err != nil {
		t.Fatal(err)
	}

	raw, err := ToRGB565(buf.Bytes())
	if err != nil {
		t.Fatal(err)
	}

	px := func(x, y int) uint16 {
		i := (y*Size + x) * 2
		return uint16(raw[i]) | uint16(raw[i+1])<<8
	}
	left := px(4, Size/2)
	right := px(Size-5, Size/2)
	if left == right {
		t.Fatalf("izquierda y derecha iguales (0x%04X): no recorto, aplasto", left)
	}
}

func TestRejectsGarbage(t *testing.T) {
	if _, err := ToRGB565([]byte("no soy una imagen")); err == nil {
		t.Error("quiero error con bytes invalidos")
	}
}

func TestKeyIsStableAndURLSafe(t *testing.T) {
	a := Key("https://ejemplo.com/foto.jpg")
	b := Key("https://ejemplo.com/foto.jpg")
	if a != b {
		t.Fatal("la key deberia ser estable")
	}
	if a == Key("https://ejemplo.com/otra.jpg") {
		t.Fatal("URLs distintas deberian dar keys distintas")
	}
	for _, c := range a {
		ok := (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
		if !ok {
			t.Fatalf("key con caracter no seguro para URL: %q", a)
		}
	}
}
