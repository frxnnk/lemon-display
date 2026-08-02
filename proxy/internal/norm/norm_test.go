package norm

import "testing"

func TestClean(t *testing.T) {
	cases := []struct{ name, in, want string }{
		{"tildes", "El niño comió mañana", "El nino comio manana"},
		{"mayus con tilde", "ÁRBOL Ñandú", "ARBOL Nandu"},
		{"emoji fuera", "Buenísimo \U0001F680\U0001F525 esto", "Buenisimo esto"},
		{"url colapsada", "mira esto https://t.co/abc123 ahora", "mira esto [link] ahora"},
		{"url al final", "buenisimo https://t.co/xyz", "buenisimo [link]"},
		{"comillas curvas", "el “mejor” día", "el \"mejor\" dia"},
		{"apostrofe curvo", "l’affaire", "l'affaire"},
		{"puntos suspensivos", "espera… ya", "espera... ya"},
		{"guion largo", "uno — dos", "uno - dos"},
		{"espacios colapsados", "hola    che\n\nque tal", "hola che que tal"},
		{"nbsp", "uno dos", "uno dos"},
		{"ya ascii queda igual", "todo normal aca", "todo normal aca"},
		{"vacio", "", ""},
		{"solo emoji", "\U0001F680\U0001F525", ""},
		{"cirilico se cae", "Привет hola", "hola"},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			if got := Clean(c.in); got != c.want {
				t.Errorf("Clean(%q) = %q, quiero %q", c.in, got, c.want)
			}
		})
	}
}

func TestCleanIsAlwaysASCII(t *testing.T) {
	ins := []string{
		"Ñoño \U0001F389 café — “test”",
		"你好世界",
		"emoji con modificador \U0001F468‍\U0001F4BB fin",
	}
	for _, in := range ins {
		out := Clean(in)
		for i := 0; i < len(out); i++ {
			if out[i] > 127 {
				t.Fatalf("Clean(%q) dejo byte no-ASCII %d en %q", in, out[i], out)
			}
		}
	}
}

func TestTruncateShortStringUntouched(t *testing.T) {
	in := "texto corto"
	if got := Truncate(in, 100); got != in {
		t.Fatalf("Truncate = %q, quiero %q sin tocar", got, in)
	}
}

func TestTruncateCutsOnWordBoundary(t *testing.T) {
	in := "otra vez con mucho texto que se pasa del limite fijado para la prueba"
	got := Truncate(in, 20)

	if len(got) > 21 {
		t.Fatalf("largo = %d, quiero <= 21 (max 20 + punto)", len(got))
	}
	if got[len(got)-1] != '.' {
		t.Fatalf("deberia terminar en punto, got %q", got)
	}
	if got[len(got)-2] == ' ' {
		t.Fatalf("no deberia quedar espacio antes del punto: %q", got)
	}
}

func TestTruncateDoesNotSplitMidWord(t *testing.T) {
	in := "aaaa bbbb cccc dddd eeee ffff"
	got := Truncate(in, 12)
	trimmed := got[:len(got)-1]
	if len(trimmed) > 0 && trimmed[len(trimmed)-1] != 'a' && trimmed[len(trimmed)-1] != 'b' {
		t.Fatalf("corto en medio de una palabra: %q", got)
	}
}
