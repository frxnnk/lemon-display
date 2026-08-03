package sorsa

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"time"

	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/feed"
	"github.com/fcedeirajoaquin/ferced-display/proxy/internal/norm"
)

const (
	listEndpoint   = "https://api.sorsa.io/v3/list-tweets"
	searchEndpoint = "https://api.sorsa.io/v3/search-tweets"
	maxBody        = 512 << 10
	maxText        = 180
	defaultOrder   = "latest"
)

// Shape verificado contra https://api.sorsa.io/v3/swagger.json
// (common.TweetsResponse -> common.Tweet -> common.User), y confirmado despues
// contra una captura real en testdata/list_tweets.json.
type rawResp struct {
	NextCursor string `json:"next_cursor"`
	Tweets     []struct {
		ID        string `json:"id"`
		FullText  string `json:"full_text"`
		CreatedAt string `json:"created_at"`
		Lang      string `json:"lang"`
		IsReply   bool   `json:"is_reply"`
		User      struct {
			Username        string `json:"username"`
			DisplayName     string `json:"display_name"`
			ProfileImageURL string `json:"profile_image_url"`
		} `json:"user"`
		// entities NO esta documentado en el swagger: figura como "array" a
		// secas, sin forma. Ver rawEntity.
		Entities []rawEntity `json:"entities"`
	} `json:"tweets"`
}

// rawEntity es la media adjunta a un tweet. El swagger no la describe: estos
// tres campos salen de una captura real del 2026-08-03. No inventar nombres
// aca — capturar y mirar, que es justo lo que fallo la primera vez con este
// mismo cliente.
type rawEntity struct {
	Type    string `json:"type"`
	Link    string `json:"link"`
	Preview string `json:"preview"`
}

var dateLayouts = []string{
	time.RFC3339,
	"Mon Jan 02 15:04:05 -0700 2006",
	time.RFC1123Z,
}

func parseDate(s string) time.Time {
	for _, l := range dateLayouts {
		if t, err := time.Parse(l, s); err == nil {
			return t
		}
	}
	return time.Time{}
}

// mediaURL devuelve la imagen del tweet, o "" si no tiene ninguna.
//
// Los dos tipos vistos son "photo" y "video". En un video el `link` es un mp4
// que el aparato no sabe decodificar, pero viene un `preview` jpg que si sirve;
// en una foto el `preview` llega vacio y la imagen esta en `link`. De ahi que
// se prefiera preview y solo se caiga a link cuando es una foto: devolver el
// link de un video le daria al pipeline de imagenes un mp4 para escalar.
func mediaURL(ents []rawEntity) string {
	for _, e := range ents {
		if e.Preview != "" {
			return e.Preview
		}
		if e.Type == "photo" && e.Link != "" {
			return e.Link
		}
	}
	return ""
}

// parse descarta respuestas sueltas: fuera del hilo se leen sin contexto y en
// una pantalla de escritorio quedan como frases al aire.
//
// mediaOnly ademas descarta los tweets sin foto ni video. En X la mayoria de
// los tweets son links pelados — medido el 2026-08-03, Ars Technica publico 14
// seguidos sin una sola imagen — y en una pantalla que muestra foto + titular
// esos items caen todos al avatar del autor y se ven iguales entre si.
func parse(raw []byte, src string, mediaOnly bool) ([]feed.Item, error) {
	var rr rawResp
	if err := json.Unmarshal(raw, &rr); err != nil {
		return nil, err
	}
	out := make([]feed.Item, 0, len(rr.Tweets))
	for _, tw := range rr.Tweets {
		if tw.IsReply {
			continue
		}
		text := norm.Truncate(norm.Clean(tw.FullText), maxText)
		if text == "" {
			continue
		}
		img := mediaURL(tw.Entities)
		if mediaOnly && img == "" {
			continue
		}
		if img == "" {
			img = tw.User.ProfileImageURL
		}
		at := parseDate(tw.CreatedAt)
		out = append(out, feed.Item{
			ID:     "x:" + tw.ID,
			Text:   text,
			Author: norm.Clean(tw.User.DisplayName),
			Handle: "@" + tw.User.Username,
			At:     at,
			Epoch:  at.Unix(),
			From:   feed.OriginX,
			ImgURL: img,
			Src:    src,
		})
	}
	feed.Sort(out)
	return out, nil
}

// fetch hace el pedido y parsea. La unica diferencia entre la Lista y la
// busqueda es como se arma el request, asi que el resto vive aca.
func fetch(c *http.Client, req *http.Request, key, src string, mediaOnly bool, n int) ([]feed.Item, error) {
	if c == nil {
		c = &http.Client{Timeout: 12 * time.Second}
	}
	req.Header.Set("ApiKey", key)
	req.Header.Set("Accept", "application/json")

	resp, err := c.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("%s: status %d", src, resp.StatusCode)
	}
	raw, err := io.ReadAll(io.LimitReader(resp.Body, maxBody))
	if err != nil {
		return nil, err
	}

	items, err := parse(raw, src, mediaOnly)
	if err != nil {
		return nil, err
	}
	if n > 0 && len(items) > n {
		items = items[:n]
	}
	return items, nil
}

// Source lee una Lista publica de X por su ID numerico.
type Source struct {
	APIKey    string
	ListID    string
	MediaOnly bool
	HTTP      *http.Client
}

func (s *Source) Name() string { return "sorsa:list" }

// Fetch ignora n al pedir: /list-tweets no acepta tamano de pagina, solo
// list_id y next_cursor. Se recorta despues de parsear.
func (s *Source) Fetch(n int) ([]feed.Item, error) {
	req, err := http.NewRequest(http.MethodGet,
		fmt.Sprintf("%s?list_id=%s", listEndpoint, url.QueryEscape(s.ListID)), nil)
	if err != nil {
		return nil, err
	}
	return fetch(s.HTTP, req, s.APIKey, s.Name(), s.MediaOnly, n)
}

// SearchSource trae tweets por consulta en vez de por Lista.
//
// Existe porque no hay forma de descubrir una Lista publica de X: Sorsa no
// tiene endpoint que busque Listas, x.com devuelve el shell sin JavaScript aun
// con un ID inventado, y los IDs no estan indexados. Una consulta
// "from:a OR from:b" hace lo mismo que la Lista, se configura como string en el
// wrapper del VPS y no depende de que alguien mantenga la Lista.
//
// El endpoint es distinto pero la respuesta es la misma common.TweetsResponse,
// asi que parse() se reusa tal cual.
type SearchSource struct {
	APIKey string
	// Query usa la sintaxis de busqueda avanzada de X: from:, OR, frases
	// entre comillas, hashtags.
	Query string
	// Order es "latest" o "popular". Con "latest" la cuenta que mas postea se
	// lleva la mayoria de los slots.
	Order     string
	MediaOnly bool
	HTTP      *http.Client
}

func (s *SearchSource) Name() string { return "sorsa:search" }

// Fetch no pagina a proposito. Una pagina son 20 tweets y con MediaOnly quedan
// ~8, que alcanzan de sobra: X es un contrapunto visual al lado de cuatro
// feeds RSS, no la fuente principal. Si alguna vez hacen falta mas, el
// next_cursor de la respuesta es por donde seguir.
func (s *SearchSource) Fetch(n int) ([]feed.Item, error) {
	order := s.Order
	if order == "" {
		order = defaultOrder
	}
	body, err := json.Marshal(map[string]string{
		"query": s.Query,
		"order": order,
	})
	if err != nil {
		return nil, err
	}
	req, err := http.NewRequest(http.MethodPost, searchEndpoint, bytes.NewReader(body))
	if err != nil {
		return nil, err
	}
	req.Header.Set("Content-Type", "application/json")
	return fetch(s.HTTP, req, s.APIKey, s.Name(), s.MediaOnly, n)
}
