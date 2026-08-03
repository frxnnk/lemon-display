package norm

import (
	"testing"
	"unicode/utf8"
)

func TestClean(t *testing.T) {
	cases := []struct{ name, in, want string }{
		// Lo que antes se transliteraba y ahora se conserva: la fuente del
		// firmware llega hasta 0xFF, asi que "años" se puede dibujar.
		{"tildes", "El niño comió mañana", "El niño comió mañana"},
		{"mayus con tilde", "ÁRBOL Ñandú", "ÁRBOL Ñandú"},
		{"signos de apertura", "¿Qué pasó? ¡Mirá!", "¿Qué pasó? ¡Mirá!"},
		{"dieresis y cedilla", "vergüenza façade", "vergüenza façade"},
		{"portugues", "São Paulo coração", "São Paulo coração"},

		// Lo que sigue transliterandose: vive arriba de 0xFF y no hay glifo.
		{"comillas curvas", "el “mejor” día", "el \"mejor\" día"},
		{"apostrofe curvo", "l’affaire", "l'affaire"},
		{"puntos suspensivos", "espera… ya", "espera... ya"},
		{"guion largo", "uno — dos", "uno - dos"},

		// Lo que se cae entero.
		{"emoji fuera", "Buenísimo \U0001F680\U0001F525 esto", "Buenísimo esto"},
		{"solo emoji", "\U0001F680\U0001F525", ""},
		{"cirilico se cae", "Привет hola", "hola"},

		{"url colapsada", "mira esto https://t.co/abc123 ahora", "mira esto [link] ahora"},
		{"url al final", "buenisimo https://t.co/xyz", "buenisimo [link]"},
		{"espacios colapsados", "hola    che\n\nque tal", "hola che que tal"},
		{"nbsp", "uno dos", "uno dos"},
		{"ya ascii queda igual", "todo normal aca", "todo normal aca"},
		{"vacio", "", ""},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			if got := Clean(c.in); got != c.want {
				t.Errorf("Clean(%q) = %q, quiero %q", c.in, got, c.want)
			}
		})
	}
}

// El invariante ya no es "todo ASCII" sino "todo codepoint que la fuente sepa
// dibujar". Un byte suelto fuera de rango en pantalla es un cuadradito.
func TestCleanOnlyEmitsRenderableCodepoints(t *testing.T) {
	ins := []string{
		"Ñoño \U0001F389 café — “test”",
		"你好世界",
		"emoji con modificador \U0001F468‍\U0001F4BB fin",
		"griego αβγ y arabe وش",
		"control\x01\x02\x1f fin",
	}
	for _, in := range ins {
		out := Clean(in)
		if !utf8.ValidString(out) {
			t.Fatalf("Clean(%q) devolvio UTF-8 invalido: %q", in, out)
		}
		for _, r := range out {
			if !renderable(r) {
				t.Fatalf("Clean(%q) dejo U+%04X, que la fuente no dibuja (en %q)", in, r, out)
			}
		}
	}
}

func TestStripHTML(t *testing.T) {
	cases := []struct{ name, in, want string }{
		{"parrafo", "<p>Hola <b>mundo</b></p>", "Hola mundo"},
		{"link", `Mira <a href="http://x.com">esto</a> ahora`, "Mira esto ahora"},
		{"imagen suelta", `<img src="foo.jpg"/>Texto`, "Texto"},
		{"entidad amp", "Uno &amp; dos", "Uno & dos"},
		{"entidad comillas", "Dijo &quot;hola&quot;", `Dijo "hola"`},
		// Antes esto devolvia "caf": las entidades numericas se tiraban porque
		// "casi siempre son acentos, que Clean tirara igual".
		{"entidad numerica decimal", "caf&#233;", "café"},
		{"entidad numerica hex", "ni&#xF1;o", "niño"},
		{"entidad numerica invalida", "roto &#zz; fin", "roto &#zz; fin"},
		{"salto de linea", "uno<br/>dos", "uno dos"},
		{"sin html", "texto plano", "texto plano"},
		{"vacio", "", ""},
		{"solo tags", "<div><span></span></div>", ""},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			if got := StripHTML(c.in); got != c.want {
				t.Errorf("StripHTML(%q) = %q, quiero %q", c.in, got, c.want)
			}
		})
	}
}

func TestStripHTMLHandlesUnclosedTag(t *testing.T) {
	if got := StripHTML("texto <b sin cerrar"); got != "texto" {
		t.Errorf("got %q", got)
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

// max cuenta bytes y un acento son dos, asi que el corte puede caer al medio de
// una secuencia. Se barren todos los puntos de corte posibles: en ninguno tiene
// que salir UTF-8 roto, que en pantalla seria un cuadradito.
func TestTruncateNeverSplitsAUTF8Sequence(t *testing.T) {
	ins := []string{
		"añañañañañañañañañañaña",
		"El niño comió mañana en el jardín con su güero",
		"¿Qué pasó ayer? ¡Nadie sabía nada de nada todavía!",
	}
	for _, in := range ins {
		for max := 2; max < len(in); max++ {
			got := Truncate(in, max)
			if !utf8.ValidString(got) {
				t.Fatalf("Truncate(%q, %d) = %q, no es UTF-8 valido", in, max, got)
			}
		}
	}
}
