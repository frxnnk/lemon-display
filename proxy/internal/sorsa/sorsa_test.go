package sorsa

import (
	"bytes"
	"encoding/json"
	"io"
	"net/http"
	"os"
	"strings"
	"testing"

	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/feed"
)

// schema_example.json esta armado a mano desde el swagger oficial
// (common.TweetsResponse). Corre siempre: valida el parser contra el shape
// documentado.
const schemaFixture = "testdata/schema_example.json"

// list_tweets.json es una captura REAL de la API, tomada el 2026-08-03 contra
// /v3/search-tweets y no contra /list-tweets, porque la Lista publica todavia
// no existe. Vale igual: los dos endpoints devuelven common.TweetsResponse, que
// es exactamente lo que este fixture valida — que el shape documentado coincida
// con el que la API devuelve de verdad.
const realFixture = "testdata/list_tweets.json"

func parseFile(t *testing.T, path string) []feed.Item {
	t.Helper()
	return parseFileOpts(t, path, false)
}

func parseFileOpts(t *testing.T, path string, mediaOnly bool) []feed.Item {
	t.Helper()
	raw, err := os.ReadFile(path)
	if err != nil {
		t.Skipf("falta %s", path)
	}
	items, err := parse(raw, "sorsa:list", mediaOnly)
	if err != nil {
		t.Fatalf("parse fallo: %v", err)
	}
	return items
}

func TestParsesDocumentedSchema(t *testing.T) {
	items := parseFile(t, schemaFixture)
	if len(items) == 0 {
		t.Fatal("no parseo ningun tweet")
	}
}

func TestSkipsRepliesAndEmptyText(t *testing.T) {
	items := parseFile(t, schemaFixture)
	// El fixture tiene 4 tweets: uno es respuesta y otro tiene texto vacio.
	if len(items) != 2 {
		t.Fatalf("items = %d, quiero 2 (descarta la respuesta y el vacio)", len(items))
	}
	for _, it := range items {
		if it.Handle == "@respondedor" {
			t.Error("no deberia entrar una respuesta")
		}
	}
}

func TestMapsFullTextAndUser(t *testing.T) {
	items := parseFile(t, schemaFixture)
	first := items[0]

	if first.Handle != "@ferced" {
		t.Errorf("handle = %q, quiero @ferced", first.Handle)
	}
	if first.Author != "Ferced" {
		t.Errorf("autor = %q", first.Author)
	}
	if first.ID != "x:1782368585664626774" {
		t.Errorf("id = %q", first.ID)
	}
	if first.From != feed.OriginX {
		t.Errorf("origen = %q", first.From)
	}
}

func TestNormalizesTextAndCollapsesURL(t *testing.T) {
	items := parseFile(t, schemaFixture)

	if want := "Publicamos la nueva version del firmware - mas rapida y con soporte de temas [link]"; items[0].Text != want {
		t.Errorf("texto  = %q\nquiero = %q", items[0].Text, want)
	}
	if items[1].Text != "Buenisimo esto" {
		t.Errorf("emoji no removido: %q", items[1].Text)
	}
	if items[1].Author != "Alguien Nandu" {
		t.Errorf("autor sin normalizar: %q", items[1].Author)
	}
}

func TestTextIsASCIIAndBounded(t *testing.T) {
	for _, it := range parseFile(t, schemaFixture) {
		for i := 0; i < len(it.Text); i++ {
			if it.Text[i] > 127 {
				t.Fatalf("texto no normalizado: %q", it.Text)
			}
		}
		if len(it.Text) > maxText+1 {
			t.Errorf("texto sin recortar: %d bytes", len(it.Text))
		}
	}
}

func TestDatesParsed(t *testing.T) {
	for _, it := range parseFile(t, schemaFixture) {
		if it.Epoch <= 0 {
			t.Fatalf("fecha sin parsear (%q): revisar dateLayouts", it.Text)
		}
	}
}

func TestSortedNewestFirst(t *testing.T) {
	items := parseFile(t, schemaFixture)
	if !items[0].At.After(items[1].At) {
		t.Error("el primero deberia ser el mas nuevo")
	}
}

func TestParseRejectsGarbage(t *testing.T) {
	if _, err := parse([]byte("no soy json"), "sorsa:list", false); err == nil {
		t.Error("quiero error con JSON invalido")
	}
}

// mediaURL prefiere el preview porque en un video el link es un mp4: si se
// devolviera, el pipeline de imagenes recibiria un video para escalar.
func TestMediaURLPrefersPreviewAndRejectsVideoLink(t *testing.T) {
	casos := []struct {
		nombre string
		ents   []rawEntity
		quiero string
	}{
		{"foto usa link porque no trae preview",
			[]rawEntity{{Type: "photo", Link: "http://x/f.jpg"}},
			"http://x/f.jpg"},
		{"video usa preview, nunca el mp4",
			[]rawEntity{{Type: "video", Link: "http://x/v.mp4", Preview: "http://x/v.jpg"}},
			"http://x/v.jpg"},
		{"video sin preview no aporta imagen",
			[]rawEntity{{Type: "video", Link: "http://x/v.mp4"}},
			""},
		{"sin entities no hay imagen",
			nil,
			""},
	}
	for _, c := range casos {
		t.Run(c.nombre, func(t *testing.T) {
			if got := mediaURL(c.ents); got != c.quiero {
				t.Errorf("mediaURL = %q, quiero %q", got, c.quiero)
			}
		})
	}
}

// La captura real tiene tweets con y sin media, asi que sirve para comprobar
// que el filtro efectivamente recorta.
func TestMediaOnlyDropsTweetsWithoutImage(t *testing.T) {
	todos := parseFileOpts(t, realFixture, false)
	soloMedia := parseFileOpts(t, realFixture, true)

	if len(soloMedia) == 0 {
		t.Fatal("con MediaOnly no quedo ningun tweet: revisar la forma de entities")
	}
	if len(soloMedia) >= len(todos) {
		t.Fatalf("MediaOnly no filtro nada: %d de %d", len(soloMedia), len(todos))
	}
	for _, it := range soloMedia {
		if it.ImgURL == "" {
			t.Error("quedo un item sin imagen")
		}
		// El avatar del autor no es media del tweet: si se cuela, el filtro
		// no esta mirando entities.
		if strings.Contains(it.ImgURL, "profile_images") {
			t.Errorf("se colo un avatar como media: %s", it.ImgURL)
		}
	}
}

// Sin MediaOnly el avatar sigue siendo el fallback, que es lo que hacia antes.
func TestFallsBackToAvatarWhenNoMedia(t *testing.T) {
	var conAvatar int
	for _, it := range parseFileOpts(t, realFixture, false) {
		if strings.Contains(it.ImgURL, "profile_images") {
			conAvatar++
		}
	}
	if conAvatar == 0 {
		t.Error("ningun item cayo al avatar: la captura deberia tener tweets sin media")
	}
}

// El mezclador agrupa por Src, asi que la busqueda tiene que declararse
// distinta de la Lista o las dos contarian como una sola fuente.
func TestSourcesDeclareDistinctNames(t *testing.T) {
	lista := (&Source{}).Name()
	busqueda := (&SearchSource{}).Name()
	if lista == busqueda {
		t.Fatalf("ambas fuentes se llaman %q", lista)
	}
	raw, err := os.ReadFile(realFixture)
	if err != nil {
		t.Skipf("falta %s", realFixture)
	}
	items, err := parse(raw, busqueda, false)
	if err != nil {
		t.Fatalf("parse fallo: %v", err)
	}
	if items[0].Src != busqueda {
		t.Errorf("Src = %q, quiero %q", items[0].Src, busqueda)
	}
}

// stubRT intercepta el pedido sin salir a la red y guarda lo que se mando.
type stubRT struct {
	got  *http.Request
	body []byte
	resp []byte
}

func (s *stubRT) RoundTrip(r *http.Request) (*http.Response, error) {
	s.got = r
	if r.Body != nil {
		s.body, _ = io.ReadAll(r.Body)
	}
	return &http.Response{
		StatusCode: http.StatusOK,
		Body:       io.NopCloser(bytes.NewReader(s.resp)),
		Header:     make(http.Header),
	}, nil
}

// Armar mal el body del POST no da error: la API responde vacio y el feed se
// queda sin items en silencio. Por eso se comprueba el request, no solo la
// respuesta.
func TestSearchSourceBuildsRequest(t *testing.T) {
	raw, err := os.ReadFile(realFixture)
	if err != nil {
		t.Skipf("falta %s", realFixture)
	}
	rt := &stubRT{resp: raw}
	s := &SearchSource{
		APIKey:    "clave-de-prueba",
		Query:     "from:NASA OR from:esa",
		MediaOnly: true,
		HTTP:      &http.Client{Transport: rt},
	}

	items, err := s.Fetch(0)
	if err != nil {
		t.Fatalf("Fetch fallo: %v", err)
	}

	if rt.got.Method != http.MethodPost {
		t.Errorf("metodo = %s, quiero POST", rt.got.Method)
	}
	if h := rt.got.Header.Get("ApiKey"); h != "clave-de-prueba" {
		t.Errorf("header ApiKey = %q", h)
	}
	if ct := rt.got.Header.Get("Content-Type"); ct != "application/json" {
		t.Errorf("Content-Type = %q", ct)
	}

	var enviado map[string]string
	if err := json.Unmarshal(rt.body, &enviado); err != nil {
		t.Fatalf("el body no es JSON valido: %v", err)
	}
	if enviado["query"] != s.Query {
		t.Errorf("query = %q, quiero %q", enviado["query"], s.Query)
	}
	if enviado["order"] != defaultOrder {
		t.Errorf("order = %q, quiero %q por defecto", enviado["order"], defaultOrder)
	}

	if len(items) == 0 {
		t.Fatal("no volvio ningun item")
	}
	for _, it := range items {
		if it.Src != "sorsa:search" {
			t.Errorf("Src = %q", it.Src)
		}
		if it.ImgURL == "" {
			t.Error("con MediaOnly no deberia haber items sin imagen")
		}
	}
}

// Confirma contra la API real. Skip hasta que se capture el fixture.
func TestRealAPIMatchesDocumentedSchema(t *testing.T) {
	items := parseFile(t, realFixture)
	if len(items) == 0 {
		t.Fatal("la captura real no parseo nada: el shape documentado no coincide con la API")
	}
	for _, it := range items {
		if it.ID == "x:" {
			t.Error("tweet sin id en la respuesta real")
		}
		if it.Handle == "@" {
			t.Error("tweet sin username en la respuesta real")
		}
		if it.Epoch <= 0 {
			t.Errorf("fecha real sin parsear en %q", it.Text)
		}
	}
}
