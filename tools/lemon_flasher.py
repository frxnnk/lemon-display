#!/usr/bin/env python3
"""
Lemon Display Flasher — GUI tool to update Lemon Box firmware via USB.

Downloads the latest firmware from GitHub releases and flashes it to the
ESP32-S3 board using esptool. Designed for end-users who just need to
plug in their device and click one button.
"""

import base64
import io
import json
import math
import os
import re
import struct
import sys
import threading
import tkinter as tk
import wave as _wave
from tkinter import ttk, messagebox
from urllib.request import urlopen, Request
from urllib.error import URLError, HTTPError

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
GITHUB_REPO = "frxnnk/lemon-display"
GITHUB_API = f"https://api.github.com/repos/{GITHUB_REPO}/releases/latest"
FLASH_OFFSET = 0x10000
FLASH_BAUD = 921600
CHIP_NAME = "esp32s3"


# GitHub token — read from the same secrets.h the firmware uses.
def _resolve_token():
    # 1. PyInstaller bundle: token.txt embedded at build time
    bundle_dir = getattr(sys, "_MEIPASS", None)
    if bundle_dir:
        path = os.path.join(bundle_dir, "token.txt")
        if os.path.isfile(path):
            with open(path, "r") as f:
                tok = f.read().strip()
            if tok:
                return tok

    # 2. Development: secrets.h relative to script or exe
    script_dir = os.path.dirname(os.path.abspath(__file__))
    exe_dir = os.path.dirname(os.path.abspath(sys.executable))
    candidates = [
        os.path.join(script_dir, "..", "src", "secrets.h"),
        os.path.join(exe_dir, "..", "src", "secrets.h"),
        os.path.join(exe_dir, "..", "..", "src", "secrets.h"),
    ]
    for path in candidates:
        path = os.path.normpath(path)
        if os.path.isfile(path):
            with open(path, "r") as f:
                for line in f:
                    m = re.match(r'#define\s+GITHUB_PAT\s+"([^"]+)"', line)
                    if m:
                        return m.group(1)

    # 3. Fallback: token.txt next to exe
    for base in [exe_dir, script_dir]:
        path = os.path.join(base, "token.txt")
        if os.path.isfile(path):
            with open(path, "r") as f:
                tok = f.read().strip()
            if tok:
                return tok
    return ""


GITHUB_TOKEN = _resolve_token()

# ── Lemon Brand Colors ──
BLACK = "#000000"
GREENT = "#00F068"
EVERGREENT = "#00A849"
NEBULA = "#806CF2"
SOLAR = "#FF8700"
STARLIGHT = "#E7E7E7"
MOON = "#5B5B5B"
DARK = "#0A0A0A"
DARKER = "#060606"

# ── Embedded logo (lemon_imagotipo_load.png) ──
LOGO_B64 = (
    "iVBORw0KGgoAAAANSUhEUgAAAPQAAAA4CAYAAADKBdUxAAABfGlDQ1BJQ0MgUHJvZmlsZQAAeJx1kc8r"
    "w2Ecx982msya4uDgsITTpqFkDg4To3CYKcNl++6X2o9v3++W5KpcV5S4+HXgL+CqnJUiUnLlTFxYX5/v"
    "tppkn6fP83k97+f5fHqezwOWUFrJ6I1eyGTzWjDgdy2Gl1y2F2zYcTCKL6Lo6uz8ZIi69nlPgxlvPWat"
    "+uf+NXssrivQ0Cw8pqhaXnhKeGYtr5q8I9yhpCIx4TNhtyYXFL4z9WiFX01OVvjbZC0UHAdLm7Ar+Yuj"
    "v1hJaRlheTk9mXRBqd7HfElrPLswL7FbvAudIAH8uJhmgnGGGcAn8zAeBumXFXXyveX8OXKSq8isso7G"
    "KklS5HGLWpDqcYkJ0eMy0qyb/f/bVz0xNFip3uqHpmfDeO8F2zaUiobxdWQYpWOwPsFltpafO4SRD9GL"
    "Na3nAJybcH5V06K7cLEFnY9qRIuUJau4JZGAt1NwhKH9BlqWKz2r7nPyAKEN+apr2NuHPjnvXPkB1ypo"
    "Gea0itwAACNSSURBVHic7X0JdBzVlfb3autF3VJLtjHbkARwMpiEMCThJCEEZ8gMHP6wZIhIAsOwS7JZ"
    "BgaSIUDG0rBMIPFPwie2JQyJ5wxMYkHCDAzLPwTbSQ5MFviTACbB7D9LbEvqRS31VlXvP/e+6nZLqu6u"
    "lmRshr4+fayurqr36tW7y7v3u/cJzIVWrtTQDw1iwK4ck1d0YSL/Ubj4OKT8M0h5KKRYDIE4pLQghASQ"
    "B5CGwBsQ+AN0/UlAPoGo/huI1YVd91ppAAMOBOiaFrWoRQ1IYDYk6bpuDWLY4e8jl7Yj5J4E4X4eEsdA"
    "EwcgrAMuANsFHKk+0uNLTaiPoQGG14XJkgshXoDQfgIp70Vs0SaIAboDILv1ClstalGL5pGhq5lrrOcg"
    "hIw+SJyNNvNAPpYrAQWXONdVd5cCUgjQvyn3gcfhrLGpJzoLgZAJ2A5QdH4HyEFkxjdgv3+dUEJkpagw"
    "eYta1KI5MHS1Vn6xpwP7mldB4GJEjE5MloCiW9ag2gzmDdyGJGalj4aoqcHSgZy9Da78BmJr7uRzNh1n"
    "4DNbdpn4LWpRiyoUjPHkSq2iGZPLT0NIfAsR6xBkC2RSE3PpEGJ2TNyIucOGgbBBjL0JefdydK39HVsJ"
    "GCYLoLW2blGLqqgxE27s1nHGsINnui28d+EqhI1LUHKBvG0DQp+1Nm6WsdstA0V3EgX7K0gMroGUql3l"
    "ZGtRi1rUkKHL6+VXz98fCyM/RNT8FFJ5h9fFQmh4O8mVDgxNR9wCssUhPDSyggVNtfXQoha9y0k0ZOYd"
    "F74fMetBhI1DkC6WIGC+rT2c0id2kztIRAxkCw8iGf4CDro112LqFrVIkVafmXuXIGY9BlM/BKmCvUeZ"
    "mYjW6UIYSOVLiIVOQlf+frx8Tlj12TPBW9SidzHNZGjWdsMOxi9YjKj+MEz9AIwXHWjCwN5CJFiIqdtC"
    "x6Mr8gOlnYc15YlvUYvevSR8Q1Po1JDRNqHdOgbJgj1vzFyJPXNomanpXBCC1przlEiUkIibGJr+NBYN"
    "XMLKsGrUW+D4rSbA18gm4u82sZ6/90j0jjKaPF4UFly1rcNFWOe9An93drqR3vFXUHOfNm4Fly1xggLig"
    "eUcrOY+7g7zDOT1DQz6c2oFNKw18ZsDGaO9t6IpcylpwPsxsCfUAulDgEVOfeQ5hUfL21Hg2pDOF0fwc"
    "cUKU0G6ZSOe/iMTgxhaq7G0imvbzFTacz3v5CepmhTwLWJpbu2ke7Uafzy6GLjNCqudExEIPIVuiQTDm"
    "gZF1xE1A14BMcRICWwG5FRL/j/HcEiak3Bea+AAgjkTM3KfC3DELKDi7WHqipBT5lDakywAUiTTyzoew"
    "YN2bQH8wRBlh0QcGXIz0LkXCPAbjBFGboaldxEM6ssWnkFj75Ly+jPJEHu09CR3mwchSPNBr35WC/3I1"
    "wtApcqT62y0Pgpz6v6ZXXVf+bfq5dIjO0QHHdWCXBrHoznF1SreOzMK/gYZ2lBxX4XOrSJMuYiEdyfxL"
    "WDR0/7wy4kjPeYgZC5EjjDAxIfWX2qf/hURU15F1XkfXug0N25V0zRm7oMlEueUHA9oSlORBcNx2CFfN"
    "baG50DAJqW2H5r6ItsjvIW7NqftAYLhb42hKLVoJDQNwkVr+WcSMw5AtOqA7TieNMBWmgUzpBewz+GDb"
    "80ieE0YqfDYMPQTHlbveL0OpyfKlESkYu0ztpRLyyjZkJtbAdiUkm7+B2/OJHQMdIR05G8g7jwL23TDF"
    "Y4iuebXmdakLulAQn4WLS6BrH8J4aT0gH4DrhKDr1yJqftrDfE/V2kXHRiLciYL9HQjxBUhaNgSgfu9l"
    "6NqJ0KOrEM8pwVNNNK/1MODmVwF40mO4eZKu1M9hB4b4O+jR49GenymwdhtpQKFIAuOHABRDAxak/A5i"
    "bXGgVCMIogFh3UXywkOB21+B7J+9gCsrkfHeZYhF7+RhJejvjPMIIRwCROYZABsYAkymcc17CmJAB+ne"
    "JTC0s+DiFJTkUkS1EMI1pga1MWkD2fyrmFi+CS7uglj7KL+felbfsuM0DGyh5++DHj0dcRqiWtNPAJ2Ch"
    "NcJEAP/J5A1ScKJsBZvtcfRZg8hFqox/QQwXihrYIJ0DjgY670SnZH3IZmb/bqZ4sVRQ2fhmi/9GJC3"
    "ILbuv6e0zAw3Y71Ba9QxWo3wR162GOK27ZVfd/Q8DeB56FobHKlkdOWO5PkuOGgPnY5k359DrHusKdNb"
    "OJNsEowXZ1olAjY6NANCTmB3EVkXbs5Gpqgsmt1OQjJA15U5WNWzI0rjOoLJXARFkmS+fgUbHeEwkk4/"
    "hDgnsPD0JVIiEEjiJpRKLiZKpRrP7yDm0vEkf+uvcTvpvfPt5+yLeNvXIXEuolYU9FoJPpEpErqwhvBh"
    "SaojpL8HEeNclJxzMbFiCwpyJcRaLRWHa03LwM0QnBEZnzlUdRIs3UDUuBvJCz8Gsf7lxpram+ZtrgtX"
    "jiCbT8Am6VMd1RGSsJpwZMZQ64wBF+OXLYYs/R0mCqQBZzeppLTRGTYwab8AW16B2NoHpjkkJHe+FqNV"
    "48WJmekFbd4hsGwfGsQRJJ0kImYMDqVw1VBlUt4IiU9yW4GJNL4nwEg4TL0fHTTUObuNyBqiNkhM7X6G"
    "ZjZiJ+TMiSel4Y3BVEtoF+kYL7iIGGdibPkqYO3TFTRhU30g5htwkFlxKjqMT7AwE6SGfftLWorGpfbY"
    "lJl5rPdUhPW1iJj7IVMAUmQikmASAho/T/33WLRd5G2lMOLWcdDlZoz3fQvDo1c3ADLVnkPVVHActFsL"
    "YJv3QJ5zDLC1FGjpIi0JMalDaAZPyqnOYzJhBISk/EWW1RJ2cQXiVgdKLkmx5uw+1RUHCWLm0g8w4h6N"
    "2Hcf4EEuDwAzaQPTjPqhmF0oU2PYwc591LFxHIKQuS8KjvR3jtFEK7qIWx9Hevnx3FbZudGQ9jB8lHwG"
    "75yAG2kCyRh74d7A76x7Fvdg7bzSgOPcwEu8uTw/RzeGHSR7r0K7dR+A/ZDM23BZcJGAaiJhSGjQCNI"
    "sNIyXHORKLmLhq/C/Fj6AnefH1bxiZ9u0PgTsK92btHjcOgqpyHo131cGmKeZIH2nBx2wIXuigLwQOZ"
    "ZMzWki5cKweb2cKd6AtjVfxnvWJjkMEYSJa921jNHuphffrcMVNyCsm5B11q/keDCEhHQvwzuJ2KnxDi"
    "I1KR1EzZORWfEpfs+BhSdHU0g7u8hsPwuJ8Acxwc7Axtf7sWQ5VDna+1UkIt9EtuSg4Li8ZJxrngEpCe"
    "IRivZEQycibN3Pzilud9q9m8kpoL6l8uT3OQujPV9RPNg4JAVZs41KXxTzpsQJ6AjtzwPRrGmpgdau1M"
    "G/R8faryutLMX8pTj2S2ym9bZY0lCSS6FjvCSga3+BsRXvURPNR5pOpxZotHmiqaULyl3/J/4+HPg6gW"
    "Wku6+IsI+4bN7Oqg9sttsYueh0dIZuRiZve87c+V0eUeg2SejEyHFIhu5USqrsO1g2yzlEc7VgI2bdgu"
    "09JyqmricUrcYCQ0rvwTXRzRJGlEuKNBGWajN1pAo3onPwFvy6x1RpjfOpcfp1jo1LrIalkyOn9qk0MR"
    "yXzBlyS5/qHdX2eg3pBhxr8lHMx4ccW+X/Z0u8xCk56Ah9CmM9J6v1ZRAt7WnnVK4X7aH3IW87gRmQC"
    "mUQ9feTua4BGynkeADC5u3I2eQ0qg9MouhLzXHxsBK1SIOJdK6EzuiXkew5b4ZVQjjFZkjNVY2VVNy4C"
    "6nlBzdWQA34kwJ9kOfH4crjkLdpNRTcbKLBIW92tvQsOtddx46RjwzZjRf3EBz/bXa6W+bDSBfynHHFi"
    "LNaJIjpJXR50pTr69IexqGUJ0OtsaOj7ZbOPoqan0gTn7CJ9oiBiBGHqBljaUzEOmw1iRs8tBt7EOu++"
    "364SJ6TgIarMVlqfolXoa0qnCPkzWgzO9m6FDUEA80XYtiwofmPV9hAzFTzqhxy9b2P1DFZdKHptyDTs"
    "5CV17K3ynH+5h+B+kv9NvUuaPJevHZFBMNb2ZM0s21PQ6sqQDX6R165VOijMMX+yLOzKbjpIwS54DVM2"
    "s82dulXX0fnDKhwRaDzyQlB565+Hcne5xE2jsBESXn1/ElDzhZw8DHIng6IgXTjtvb0GrZO8zRSpiaQK"
    "fwAQrw2Mw5Offe+Vgs6sjrcWtKdw1Ykx0roNNNz6LiOiZKDzvARyHSdhY6Bf6lbUWbzSh0DAzYu77kC7"
    "eHF7IGu5xGe0v3qL/3k0bYx3vdBaNqXkC7Qmln3vY6FuyZYII4XX0EhvwUQL8NFaZcokYsAcTRC+idhC"
    "MEAHuUR92NAG52RhUjlLmHx9HKB2qVw2+zmEPWbwqWdkSMhJ9fjjOGzvOKY08YwBSDSQEMLUHz1E4gYY"
    "O92czFQMl+IQduYWSh63Cg4PnLhgYha9yFbPBNi/fPBY8UeAEPiJZjaESxMapnSxLolRyJidCHjHg7g8"
    "cr1ey/V42jJSw1bvw6J1S/uptZnj/iiK2kdDNEPecUw0F6A3DLzfmxK9jvI7twXUv4th0dpHTkr8rSiL"
    "fuQoCVf3l8wuN7YCVlAtngNcmIIiwezNW9LABfoq9AROgqZAi0FZvaPLIocKRRcAHnFzfy8cyVykqXzJH"
    "REzsTOnt9BDNw8Iy8hEZLM0/WINTTkUU2/ShpOS7c4hCTlksaMSSHPlRpeeWUElqkjFnkYY91/BjGcDs"
    "bUFRDKmLpXPQXNROaVgXzeY+hGoHltb/Yyk4CiR7oGqb6XldCVsgL/1KqEZvlvhgVqni4vw0J3lW1Uv0"
    "mCWDjIiG9DrE3OvneCLCKHAUmpyT503norZwTM0DBsIrtI9l6DRLijJhPWHwmPhmwFhcQpDBH2zRoEWT"
    "YSmsihYH8OnUObGiSBELBpM976608jHn8AcWsZA1Gma2p63rxN4dEDkc59GolbH/GubkxkztdeFhgYz9d"
    "ot76B0YuIqR/ivmKL4o1kgHU6FdKFxBIuKaQws407RQgwxy2iYD8NQ/sIDO0QjC98PyR+X7MqJ0lr0gL"
    "v25DHSO/nYOIlhBY/hJ3nnwBx53hgTd2s80pgCd4JVG8y0DKo6ALtofMVpLD+MjU40XukKlL2hgoCa/a"
    "30jBZlNC0q5Fa/n3cShh9zyojKoOXCE/tiIsYLzBr7ezNp5HwEYjqf+ItFf2YxEWbpSOZuxILhjbh+Ut"
    "DWLKyCEHLgS3+91VacQJvnncGEHkGlr4Itk/kh0ShwTjz4wE8EnhuhgwNBfLq+2gjdpJJDSVXImLdhe0"
    "XHI3Fd7zg1c9TvNHIZ+15ufflmtlBQMTKq02N/xyZ6LGw5ZuIRchT1+2ZWLUdHGWgx8LBN5DJnYVo5B"
    "OIRh7A62cv8Lx7daT1Vu9JZELdq0FXyz8L7YBKalzd56rnOn8bqNFkoOehuC+tOUmz8f9z+eRLyOQI6p"
    "qDxbjnuRELHUJAhfaB41zJCS+Unz5FO0Ni0h1AmxmGMwvw0vQiFro4AhHCfXNGnr/DNp1/Dl37DfG8Y2"
    "ZupOEYk2Fi/+/tRNG9FW0mXeGf6kuWjxRHBu87JIr2cwhRIfoajjeVl+DC1DoRse7BG4QP8cgNNU7rZLu"
    "MdrRQSKUAAyzJhKE/NqrSP/i2CmCIi9gBxevqOvcpM+7i9fcgmboO0bZPoyu+GaN9H6zE4Xzd9sNewX3"
    "xJ1y4v2EtNEFIMzI3u/g757nu7SSCgByMefmQZaagpgYKzjxh1ITO62JLvwwTF+8PnKEQVRT9oPc31vt"
    "hhI0vIzMHaPEUku+rM2YuLNYP/64sxqVN+Aj2I1+NgOHex4U9VF+nXSsFz0MhD1RmcUNyELdIr38NBWc"
    "jOiIaHB9BVO0ki1sfRlTcMSWU1egJJEkEiVDd2G7lZApqaQYyhQIkHuGHTuRWIz3xNNpjB2JU/oMavAY"
    "wtjIqpuv2G5FM3YJI2wcR1n6GzPILKsgyYmwaKHoQNjm6NaSWd/J6ndIplY+2PikhpVA9ez01Gf/fG4md"
    "ka6LmBVH0b6OGWjzZo1hoSoQcz0ius6w0XlZM4hE/Rku6fN88/f1ChzYeAuOO8YRBr/Xo3gmhqWHqzn"
    "GPuUGIByifPZ8TBRfY/yGW0NTs5OsQMU7voSx3q8xTxiTJITrpIyWNXRgIvwu5x3/Hp2Dr3CeqNhAe1"
    "RdiByXA7oYOy/6WGPEC9GACsp3Df090pmbEbUSiFvrkV1xL7J95CxzOPRRwYAPO3CdwxG3OjlPd95LB+"
    "/psNU7jGriABj9RHHVC5C++P1YtkW9O4KHRsyT64aX6mILyrdv1p+i+cd0g5Ao0LUN+ENI7OOFkhr1h4S"
    "ChijvApMrfQGuzMPSyaks6zrJYtZNGO07AYk7xjhxpn4bDFovTM9jr9F5elHU0Fb+umgHpUHqSAz+Evn"
    "C5QiHQwibGyBXxBjBUw/xwubPsNLEicGrkZ5cjpxdQlv4r+CK/0am7z9YY5OZllpxKJI9n4Gp3eBJ92D"
    "EPgveFG/vp9oY3b2PaFx1WtL4/EaCllL72kwLTun6ijO0ZN+k9jCr8Zx0lOK/zeSCuzIVYJlyWPPhuJU"
    "qMciNHMhrWXJS+TmxFM9kgde8ORZIwBAiTWDR7b/CROFCL83Y3/SuOMkciZB2N/OCRKai6f2vYUf4uDf"
    "5G7nQyv+rAgXk+i+vibtuX4Nk+mbE4och435PmQY1EC+7GldZVMzUQ+uQd45HPv8k4iEL8cjJiJnrIfA"
    "UpPMMItZjsIzjkCsFqwfO9+amlfeWTL/6z7b3M9RugX5KGyE9+LPTZHLdDFyZVkkwPnOGNHA67yJqdbM"
    "wHu85FonIsQwT9UsNJQ1laoQ42wHp5oIpF6aX6pjcGgrkwcdp7OQip2pgTf0W+SokNLcbMUuD9FvrCsk"
    "bLUr5enNlily1lqdKtYvW34VU/ha0hw24/E5QA8giYWldELiHMR/Kge1/e6nc8X9UXB9kUnM1mJS/+Xz7"
    "1UinVqE98QWk+9Z6aWGNTZ4yU3et+xle3PlJTBYuxmT+N1zpJBHW0B4KcZUSVamkcRdR9Si0XS1Rw+Jz"
    "ezlVoJ+RWcA860A/41YERRnMQUWMx6WexJuA/KbyANcyFynhXhMQ7mq44p+4ZM6UhPwq4rRswliLGwF"
    "R9N7x1PuWr6yeS0L8lvdU8wt/CUEIRvK6H4oxXO7NRSp11cCZ2mNCDJXwRs9BsIzLkC2CI0av4Rym3v"
    "zfKceC0mjIU4S05Cw8xDUElKCdSRQHz5Zo7A+Foe3DIUzfJafCZlCK2TZGX+WDmn3TRCibVZ75LAavQq"
    "ovhY749cheTDHpPuXYGqgPCy178sRAEcAabOwexImLjkHOPhq2+1mE9L9EkUVTkyS2BXykIPfWvNAaOes"
    "wN+on72l1m/XqY0mYOkE/N0CKl6GzVaWcKYyX8yYS/+3dU+HovL89v8uU3zzopyvINzEauNtcxER2Qoa"
    "/i3Thr9FmfmBGSSgi0sS071nIOJHn+URRhbZmPBsouYM0+v1wcS8M7Tt1nUvleUystM14BgsLryBivpeB"
    "HjP6IDSu7xUxbkKyZxvEwH3AQI0Kq14lTmLmN89bhA7jRzD1BLI+wBJvNFFyycB9NPDYqY6rdsf/IIH"
    "9XJXTsOMsTBR/iah5KKeR+rWnCQIXub67uE65varE8RQETg/YIfqoUFBNpl53A5J9zyEaugfJvk6IgS96"
    "wIJGMUB32r7TP+XPWK+JePgEjnMGdeIJz+Ry3GenxrFrkBvIMpnwoHjzkBY6MO173e6VoZ/Xo3MPQz/5"
    "DGGi8zspJHu/Dksf5knoN8WIfwscY6RlUg3tLAWblIa8DhYBXYJ2uMfA+1cXkOq9D2Hjcmbo6XND0Hre"
    "1agyNCzjR5hYcRPcwmqIO7b7P9tGHeNbToHu3sLakMAvfsxF1kTYEJgovopU/ueVai3N+kHK0RwxnMTY8"
    "tOhySdgamEv2cVnvNjf1ZAMSPEEcjyS9ZlFmUY0+Zf4MolydDmQVNhg3b0Y6z0SGo5VPzZg5un3IOjgs"
    "gEH6Z6PwDL+kT2nQZFFDPnTNeScMWjhZ6fEsWdHGgolEq4nItlrqTIzUtV1rEzl6hdQDbWcflxIxAwN6"
    "dwGLLzjOa5/rqZxvfEh0AbhJ/4Ryd7X1Dq0PHmqKntWqn2SOKO1rpfHVD6upLuHq/fAIHSOqd0IseaPw"
    "YdD2hjr6UBi8F4ke59CzDqKEzT81sf1/B1kYtLyIZn/EbqGnsboisOhuarWWUNKqvepyUFMFC9hxlPref"
    "GtfUpRVDDZjtC1GHf7kOp7HAKvQMoSHycILLAAmcc+gqi5lMV1LWbmfnvVOwvOECMfVcGDxqKIezZtiIQ"
    "317sGfofR3vOQCP0Q2SI5zmZZbVcKA4nCr5E030TE2F8lm9es1UULdPrrKIbRidUFX8lO0Dplfv8WwG8D"
    "Z2FNB/H/8cUoYvoG6MJAnhgosAuU6l3pKBWfZE0SqFxqHenKWOUSEDGPRcg4tjb0Mkgs36teicKvADyHb"
    "aFpk8bn3vQ+CJrbHjpzF/RzHimbG2I/SjOk6apsVUpcA4GHZ9EqrUE1LgoonX/geTRKS6qACMByhEQM/"
    "R5jvf+Czsj5NQtbCm/eJPMOLH0BYsbJvr4YEppkYvPz1WJm6SJk6Mjk/wiItWpN3us0MQFmNsy5/gw53"
    "YjR3g+jK3JNU1lo08hgLHWybwvCxpdQ4LrU/jcqg9LbzINglI4G8LOaWUwVdAtB/prwApZDXbS+TPfdhZ"
    "i1lF9ErdhlPTSbJv/TOxCg7G6DLtIEoNpS9JkbqeqVmmwuQ6cM/dwd3nhdb34JYRtqwBLrHsFY70/QETo"
    "+ZnaSH5HnuIO1851YsF5ZUWlazze6rprjuSaZhuz2ryFbOBlhfWHdnGiaQxSCShPT+o5j40onVB44apho"
    "lq5E11AScqScA94kkZN2yzTHMjP1tUj1fQiJ8MlcE63pyruVB5DDLG3KFSFqng/CmZIZd2lDj2G96p61"
    "y8mo7WYyy7+P9tCpjFtuiplJ8msEbijAxb97xwIwYYD1D9dfmyvksvz/9NhMkPbnEfo5pU/k3JkDabhG"
    "FW4MGBYi09jQyGE1AcjrZ73JIFtdWwXi63ag6J7LjkNdo6oldawt/ldrHOszsyspvdHEWP576Bq6e0ZCU"
    "dk5OatngeTCDySgXPdsZEt/QMykxKmmcfbqIRLyEaQLbyJEVebrVWwQOhcjB05E6vIOpYnnAbVVrtpI8b"
    "nx5fcgbp0zq/Q61oAm7TDxKKPZyGHxTtpmdu+Phs98bwwssu9BR5hitgG0vXQQC2lw3DXoGnoN6J397i"
    "y8Bj3OwILBBzFevBRxy4BOhR3qzOFmSUXbS+iMmBjPP4Iu2etlQE1to75zXlZ2PqlFnNCyVaBrKI1i8X"
    "TYMgvTq74TrJ/MoCoUIwYmAbEeEaNGdknlKqr0SD3cqNanG6m89+ynYbmgIBd6u+AwLGr7KWLW6bNkZm"
    "WSkRNIaLfx96DlZfdUTbEl8yDd9ygdrgAbuvg6JktF6Kzl6uGNyWOvYTw/Bk3ewu9+M4Vw5kAEEeaY7u"
    "A/I5m/mFMUI4bGYI0gcNJGm0YQC1LcPlv4MUYnT+NcbEJCzmbe60EF1PpnMVk6h0sm6YwkC9yWWl9ydo"
    "m1BuPFNEyt7DH0a5EhZRD4DzYPnnyUBIL3IeakT4OaUup8ryA5mSy0hcfySxENPYGw/rE5MDNpZypc90"
    "t0rPmvyra4QWhP1zJ559gQ0+gMVf2yY90fULC/hzhr6dqjyWmNFqXbrkL70Ag29+vzkglHCoEYoWtwDbK"
    "lv4TAS+iMGAwnVVYDWZJBmELVFKNrSDNSjDykO8gWBhBf+1fs1VYADp97NVIKVWyxLICAWjj0I6SLA4w"
    "kqzem0xqhhbjyGMZv246x3v+NzsgAewz9mUpBKnmzLzZl/V9GveC9CmGpoPqVI6ciJ7+KmPlxZItQ8MA"
    "5bl2riWt5wJvZokWn56hAIrEbieCbnNc55ShVQHt72m9crUWIMjyUJrY2bbJzWUDf7WxGxfXIFs6EoVMo"
    "lfN8U88TFL/VkMm/Acde7WETFJqQiSCoDrWtwnvVa2vhjRuPUz1G6Kboyn/h1TM/iq7Or0ETFyERTnCq"
    "I1U2sQm1whu7zUSiUSCSLAyKMZOWp/MLzoMoFvuRuP1XXr9q19/W6rxDGlMSBIHZsuIk60eq7wgkwp9H"
    "Kl9orONFOXuDwgA0wNlVyEyci6jxXi6LOsNRIOlBKc57HZIXb4XIvwLoB0PTv8LF9l25DvHRX0AMEK9/"
    "rJlEbxZ2HIe+eBG3HmQgbKkGcPNlqdwN91qZRZ5iqU/wYXUOPNr2lrCvDoMJq7dKovdHYPBBvMxMC3IK"
    "35UumHEduf1va96NsrkqATgrIyEJEIwai0zer4wwKIDW5CFqhitlIIWzUsfCMN5Dsuw2J+LWQfpvu0fUhI"
    "D9+I+92yVsX0+T3GinZBhaEzdrjZgFyfCaoyQ9GLO4mDP9XMXHpbciVvggpT4PEhxE147BofIU/Cm7Cdm"
    "C72+Daj6Lk/hs61j4+bQO82uTQNnU13iFnSUUAw/bf6sfniTl0y3i+nX+DbPEXSMSWBsE0eaYvSd+tZK"
    "JOINWzAob5kPcAfvWUqN8fgO38GtIcgSEWqSomtDmdfSbSC7Yh3fcUXLkN0JKAS/DSLghxCDLblwL4U7"
    "SHtErcj+uczCHhnUyjkCYwUUojFLpMSdL+oGrOg1CKJ+BMXs0J7fTM0ytm8veqA76/0x/OTJhl9e+kZW"
    "KUHGt4GGAGI9N564Hc41zDatf2r7vS8spwTdo6e0rbXju0Budt7rzv/BQEqJh2ffV9y8c69Nd3PciOEtB"
    "5FXK5OJfKmeKNp50hyVGPSST2U9utVtaRVMyAuMT9JsbHi1zudsr4cJsaZCGHEetOdS5p5yrQkZ5/Eyn"
    "0sEifzjr0XGFHg67tmNquD4myo5YQh6vp2WjX0FVcdMGxD0FaOxBw2uES49GYUKKFmADEDkC+jLbRFyr"
    "KoBJGLffVl8qx67vg5J5jK3M639COLu2ksOQvplxTj9jKpJJea7J468LPIVI6BZk87ZRa1wk9uw3faZ1B"
    "wXfKOKHQbMW9LskhQSaLT1OSNnPnDfo8k25+djgob/g+lj8TCwb/rbXhe4sqRMs62jKYhEczTixaj9Oea"
    "s1uwLcXVGSdytBlyUaQxIy2Ce3WMUgW/APc7GRgB4HwqWyoSvzOhI6WdzaYnwIFFE5IUGxwcjUWDF42o"
    "/Rp0y/+7aJpyRm+Poe3i6ZN9tpVMXdRrTHm+dNo47UazBXo2rIfZha0kt7xVsHbKvk+HkGZCSTiVSxpl"
    "pQ215pMypnHeyvyyYDxoJLjFyyGCD2OkH4wm6JzMYt3BzEzh2l7kgfQse4U4AxCrc0unNCiFv0PIZ/dAc"
    "pe7zu2Y9I5ESXnDcQtqn80T5vPzSMzjxc2IxM9Qx2cZWywRS36H0RaXW/hPoPbkC3+OUrOi0iECIqmnD"
    "h7itTeQ7YX6H8YL41/jquPon9XDegWtehdTHXqfpWZev3z2Jn/NHLOz5mRVCmctx8KQaEpyriilLts/g7"
    "8euvJOPJfJ4JlU7WoRe8OauyIKSdwP9Nt4b0LVyFsXMKe7bxNRZsI+rl7nTlKeNDWIwaKbh6O/RXEB/9"
    "ZOVFqoXZa1KJ3JwXMkKnSgsnlpyEkvoWIdQiXmbFdWltT4F3sFkamParCBjBp/xTFwt+ic/1vKuD41pq"
    "5RS2aQsGZsLo80Is9HdjXvAoCFyNidDI+okioByZCfIk5MTFXMDY1LkqXK70IV3wDse+u53PqbVfaoha9"
    "y2kW+wtVATfGeg5CyOiDxNloMw/kY1Tdo8BYZa9Aj5dnPZ3JVRxbIXVUT3Qu5E+gFMLeFp2naZtBZPF9L"
    "F6T9eKU/pvhtahFLWKapSadUswPGLm0HSH3JAj385A4Bpo4gJmTWI+Yk6rL0KcMNuGKIrT3pUYF1tUx2s"
    "RdEy9AFz+BkPfi/pFNFaROC/3VohYFormte8sIq2rkkLyiCxP5j8LFxyFwJFx5KCT2BRCHlBafQ3v2Sqe"
    "hUd1s8Tx08SSgP4Fo5jfe9jrevXif4eZgey1qEd699P8BN7c5PyMa5noAAAAASUVORK5CYII="
)

# ── Recovery: full flash files ──
# Required for recovery: bootloader, boot_app0, partitions, spiffs
RECOVERY_FILES = ["bootloader.bin", "boot_app0.bin", "partitions.bin", "spiffs.bin"]


def _find_recovery_bins():
    """Find all recovery binaries. Returns dict {name: path} or None."""
    # 1. PyInstaller bundle (embedded via --add-data)
    bundle_dir = getattr(sys, "_MEIPASS", None)
    if bundle_dir:
        found = {}
        for name in RECOVERY_FILES:
            p = os.path.join(bundle_dir, name)
            if os.path.isfile(p):
                found[name] = p
        if len(found) == len(RECOVERY_FILES):
            return found

    # 2. Development: PlatformIO build output
    script_dir = os.path.dirname(os.path.abspath(__file__))
    build_dir = os.path.normpath(
        os.path.join(script_dir, "..", ".pio", "build", "matouch_esp32s3_40")
    )
    found = {}
    for name in RECOVERY_FILES:
        p = os.path.join(build_dir, name)
        if os.path.isfile(p):
            found[name] = p
    # boot_app0.bin is in PlatformIO packages, not build dir
    if "boot_app0.bin" not in found:
        pio_boot = os.path.normpath(
            os.path.join(
                os.path.expanduser("~"),
                ".platformio",
                "packages",
                "framework-arduinoespressif32",
                "tools",
                "partitions",
                "boot_app0.bin",
            )
        )
        if os.path.isfile(pio_boot):
            found["boot_app0.bin"] = pio_boot
    if len(found) == len(RECOVERY_FILES):
        return found
    return None


# ---------------------------------------------------------------------------
# Helpers — serial port detection
# ---------------------------------------------------------------------------


def detect_esp_ports():
    """Return (ports, descriptions) for USB serial ports (skip Bluetooth)."""
    import serial.tools.list_ports

    # ESP32-S3 native USB
    ESP_VID = 0x303A
    # Common USB-UART bridges: CP210x, CH340, FTDI
    UART_VIDS = {0x10C4, 0x1A86, 0x0403}

    ports = []
    descs = []
    for p in sorted(serial.tools.list_ports.comports(), key=lambda x: x.device):
        # Skip Bluetooth ports
        if p.hwid and "BTHENUM" in p.hwid.upper():
            continue
        # Prefer known ESP / USB-UART chips, but accept any USB port
        if p.vid is not None:
            label = p.description or p.device
            if p.vid == ESP_VID:
                label += " [ESP32]"
            ports.append(p.device)
            descs.append(label)
    return ports, descs


# ---------------------------------------------------------------------------
# GitHub release helpers
# ---------------------------------------------------------------------------


def _gh_headers(accept="application/vnd.github.v3+json"):
    h = {"Accept": accept, "User-Agent": "LemonFlasher/1.0"}
    if GITHUB_TOKEN:
        h["Authorization"] = f"Bearer {GITHUB_TOKEN}"
    return h


def fetch_latest_release():
    """Query GitHub API and return (version, asset_url, md5 | None)."""
    req = Request(GITHUB_API, headers=_gh_headers())
    with urlopen(req, timeout=15) as resp:
        data = json.loads(resp.read().decode())

    version = data.get("tag_name", "").lstrip("v")
    body = data.get("body", "") or ""

    asset_url = None
    for asset in data.get("assets", []):
        name = asset["name"].lower()
        if name.endswith(".bin") and ("firmware" in name or name == "firmware.bin"):
            asset_url = asset["url"]
            break
    if not asset_url:
        for asset in data.get("assets", []):
            if asset["name"].lower().endswith(".bin"):
                asset_url = asset["url"]
                break

    md5 = None
    m = re.search(r"\b([0-9a-fA-F]{32})\b", body)
    if m:
        md5 = m.group(1).lower()

    return version, asset_url, md5


def download_firmware(url, progress_cb=None):
    """Download firmware binary and return bytes."""
    req = Request(url, headers=_gh_headers("application/octet-stream"))
    with urlopen(req, timeout=120) as resp:
        total = int(resp.headers.get("Content-Length", 0))
        data = bytearray()
        while True:
            chunk = resp.read(8192)
            if not chunk:
                break
            data.extend(chunk)
            if progress_cb and total:
                progress_cb(len(data) / total)
    return bytes(data)


# ---------------------------------------------------------------------------
# Flashing
# ---------------------------------------------------------------------------


def flash_firmware(port, firmware_bytes, progress_cb=None, log_cb=None, recovery=False):
    """Flash firmware_bytes to the ESP32-S3 at FLASH_OFFSET.

    If recovery=True, erase entire flash and write bootloader + partitions +
    firmware for a full recovery of bricked devices.
    """
    try:
        import esptool
    except ImportError:
        raise RuntimeError("esptool no esta instalado.\nEjecuta:  pip install esptool")

    if log_cb:
        log_cb("Conectando al ESP32-S3...")

    import tempfile

    def _run_esptool(args, log_cb=None):
        """Run esptool.main() capturing output, raise on error."""
        old_stdout, old_stderr = sys.stdout, sys.stderr
        capture = io.StringIO()
        sys.stdout = capture
        sys.stderr = capture
        error = None
        try:
            esptool.main(args)
        except SystemExit as e:
            if e.code not in (None, 0):
                error = capture.getvalue()
        except BaseException as e:
            error = str(e) or capture.getvalue()
        finally:
            sys.stdout = old_stdout
            sys.stderr = old_stderr
        output = capture.getvalue()
        if log_cb:
            for line in output.strip().split("\n"):
                line = line.strip()
                if line:
                    log_cb(line)
        if error:
            lines = error.strip().split("\n")
            raise RuntimeError(lines[-1].strip() if lines else "Error de esptool")

    # Write firmware to temp file
    tmp = tempfile.NamedTemporaryFile(delete=False, suffix=".bin")
    tmp.write(firmware_bytes)
    tmp_path = tmp.name
    tmp.close()

    tmp_files = [tmp_path]

    try:
        if recovery:
            # Find all recovery files
            bins = _find_recovery_bins()
            if not bins:
                raise RuntimeError(
                    "No se encontraron los archivos de recovery.\n"
                    "Necesarios: bootloader.bin, boot_app0.bin,\n"
                    "partitions.bin, spiffs.bin"
                )

            # Step 1: Erase entire flash
            if log_cb:
                log_cb("RECOVERY: Borrando flash completo...")
            _run_esptool(
                [
                    "--chip",
                    CHIP_NAME,
                    "--port",
                    port,
                    "--baud",
                    str(FLASH_BAUD),
                    "--before",
                    "default-reset",
                    "--after",
                    "no-reset",
                    "erase-flash",
                ],
                log_cb,
            )

            # Step 2: Write ALL partitions in one shot
            # 0x0000  bootloader
            # 0x8000  partitions table
            # 0xe000  boot_app0 (OTA selector)
            # 0x10000 firmware (app0)
            # 0xc90000 spiffs (cert bundle)
            if log_cb:
                log_cb("RECOVERY: Flasheando imagen completa...")
                log_cb(f"  bootloader.bin  -> 0x0000")
                log_cb(f"  partitions.bin  -> 0x8000")
                log_cb(f"  boot_app0.bin   -> 0xe000")
                log_cb(f"  firmware.bin    -> 0x10000")
                log_cb(f"  spiffs.bin      -> 0xc90000")
            _run_esptool(
                [
                    "--chip",
                    CHIP_NAME,
                    "--port",
                    port,
                    "--baud",
                    str(FLASH_BAUD),
                    "--before",
                    "default-reset",
                    "--after",
                    "hard-reset",
                    "write-flash",
                    "--flash-mode",
                    "keep",
                    "--flash-size",
                    "keep",
                    "0x0",
                    bins["bootloader.bin"],
                    "0x8000",
                    bins["partitions.bin"],
                    "0xe000",
                    bins["boot_app0.bin"],
                    hex(FLASH_OFFSET),
                    tmp_path,
                    "0xc90000",
                    bins["spiffs.bin"],
                ],
                log_cb,
            )
        else:
            # Normal flash: just firmware
            if log_cb:
                log_cb("Flasheando firmware...")
            _run_esptool(
                [
                    "--chip",
                    CHIP_NAME,
                    "--port",
                    port,
                    "--baud",
                    str(FLASH_BAUD),
                    "--before",
                    "default-reset",
                    "--after",
                    "hard-reset",
                    "write-flash",
                    "--flash-mode",
                    "keep",
                    "--flash-size",
                    "keep",
                    hex(FLASH_OFFSET),
                    tmp_path,
                ],
                log_cb,
            )

        return True

    finally:
        for p in tmp_files:
            try:
                os.unlink(p)
            except OSError:
                pass


# ---------------------------------------------------------------------------
# Chiptune synthesizer — keygen style BGM
# ---------------------------------------------------------------------------


def _midi_freq(n):
    """MIDI note number to frequency."""
    return 440.0 * (2.0 ** ((n - 69) / 12.0))


def _generate_chiptune():
    """Generate a chill jazz chiptune WAV — 8-bit keygen soul."""
    RATE = 22050
    BPM = 116  # laid-back jazz tempo
    SIXTEENTH = int(RATE * 60 / BPM / 4)

    # ── Notes (MIDI) ──
    R = 0
    # Bass octave
    C2, D2, Eb2, E2, F2, Gb2, G2, Ab2, A2, Bb2, B2 = (
        36,
        38,
        39,
        40,
        41,
        42,
        43,
        44,
        45,
        46,
        47,
    )
    # Low
    C3, D3, Eb3, E3, F3, Gb3, G3, Ab3, A3, Bb3, B3 = (
        48,
        50,
        51,
        52,
        53,
        54,
        55,
        56,
        57,
        58,
        59,
    )
    # Mid
    C4, D4, Eb4, E4, F4, Gb4, G4, Ab4, A4, Bb4, B4 = (
        60,
        62,
        63,
        64,
        65,
        66,
        67,
        68,
        69,
        70,
        71,
    )
    # High
    C5, D5, Eb5 = 72, 74, 75

    # ── Lead: jazz arpeggios with chromatic passing tones ──
    lead = [
        # Bar 1: Cm9 — relaxed arpeggio
        R,
        R,
        G4,
        Bb4,
        C5,
        R,
        D5,
        C5,
        Bb4,
        G4,
        R,
        Eb4,
        G4,
        Bb4,
        G4,
        R,
        # Bar 2: Fm9 — smooth climb
        R,
        R,
        Ab4,
        C5,
        Eb5,
        R,
        C5,
        Ab4,
        R,
        F4,
        Ab4,
        C5,
        Ab4,
        R,
        R,
        R,
        # Bar 3: Dm7b5 — tension
        R,
        R,
        F4,
        Ab4,
        C5,
        Ab4,
        R,
        F4,
        D4,
        F4,
        Ab4,
        R,
        C5,
        Ab4,
        F4,
        R,
        # Bar 4: G7alt — bluesy resolve
        R,
        R,
        B3,
        D4,
        F4,
        Ab4,
        R,
        F4,
        D4,
        R,
        B3,
        R,
        G3,
        R,
        R,
        R,
        # Bar 5: Cm7 — melody hook (chill)
        C5,
        R,
        R,
        Bb4,
        R,
        G4,
        R,
        Eb4,
        R,
        D4,
        Eb4,
        G4,
        Bb4,
        R,
        C5,
        R,
        # Bar 6: AbMaj7 — dreamy
        R,
        Ab4,
        R,
        C5,
        Eb5,
        R,
        C5,
        R,
        Ab4,
        Bb4,
        C5,
        R,
        Eb5,
        C5,
        R,
        R,
        # Bar 7: Dm7b5 — G7 — moving
        D4,
        R,
        F4,
        Ab4,
        R,
        C5,
        Ab4,
        R,
        G4,
        R,
        B4,
        R,
        D5,
        B4,
        G4,
        R,
        # Bar 8: Cm — rest and breathe
        C5,
        R,
        R,
        G4,
        R,
        Eb4,
        R,
        R,
        C4,
        R,
        R,
        R,
        R,
        R,
        R,
        R,
    ]

    # ── Walking bass: quarter notes (4 sixteenths each) ──
    bass_walk = [
        # Bar 1: Cm7
        C2,
        Eb2,
        G2,
        A2,
        # Bar 2: Fm7
        F2,
        Ab2,
        C3,
        A2,
        # Bar 3: Dm7b5
        D2,
        F2,
        Ab2,
        B2,
        # Bar 4: G7
        G2,
        B2,
        D3,
        Gb2,
        # Bar 5: Cm7
        C2,
        D2,
        Eb2,
        G2,
        # Bar 6: AbMaj7
        Ab2,
        C3,
        Eb3,
        D3,
        # Bar 7: Dm7b5 - G7
        D2,
        F2,
        G2,
        B2,
        # Bar 8: Cm
        C2,
        G2,
        Eb2,
        C2,
    ]
    bass = []
    for note in bass_walk:
        bass.extend([note] * 4)

    # ── Repeat pattern 3x for a longer seamless loop ──
    lead = lead * 3
    bass = bass * 3

    # ── Synth engine ──
    total = len(lead) * SIXTEENTH
    buf = [0.0] * total

    def add_voice(note_list, vol, duty, octave_shift=0, attack_ms=8, release_ms=15):
        att_s = int(RATE * attack_ms / 1000)
        rel_s = int(RATE * release_ms / 1000)
        for i, midi in enumerate(note_list):
            if i * SIXTEENTH >= total:
                break
            if midi == 0:
                continue
            freq = _midi_freq(midi + octave_shift)
            period = RATE / freq
            for j in range(SIXTEENTH):
                idx = i * SIXTEENTH + j
                if idx >= total:
                    break
                t = (j % period) / period
                val = vol if t < duty else -vol
                # Soft envelope for chill feel
                env = 1.0
                if j < att_s:
                    env = j / att_s
                elif j > SIXTEENTH - rel_s:
                    env = (SIXTEENTH - j) / rel_s
                buf[idx] += val * env

    # Lead: mellow square 50% duty, soft attack
    add_voice(lead, vol=0.16, duty=0.5, attack_ms=12, release_ms=20)
    # Lead shimmer: octave up, very quiet, thin
    add_voice(lead, vol=0.04, duty=0.125, octave_shift=12, attack_ms=15, release_ms=25)
    # Walking bass: warm square 25% duty
    add_voice(bass, vol=0.12, duty=0.25, attack_ms=5, release_ms=30)

    # ── Convert to 8-bit unsigned PCM WAV ──
    audio = bytes(max(0, min(255, int(s * 127 + 128))) for s in buf)

    wav_buf = io.BytesIO()
    with _wave.open(wav_buf, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(1)
        wf.setframerate(RATE)
        wf.writeframes(audio)
    return wav_buf.getvalue()


class ChiptunePlayer:
    """Chiptune player. Writes WAV to temp file so SND_ASYNC works."""

    def __init__(self, root):
        self._root = root
        self._tmp_path = None
        self._playing = False
        self._duration_ms = 0
        self._timer_id = None

    def start(self):
        if sys.platform != "win32":
            return
        if self._playing:
            return
        if not self._tmp_path:
            import tempfile

            wav_data = _generate_chiptune()
            with _wave.open(io.BytesIO(wav_data), "rb") as wf:
                self._duration_ms = int(wf.getnframes() / wf.getframerate() * 1000)
            fd, self._tmp_path = tempfile.mkstemp(suffix=".wav")
            os.write(fd, wav_data)
            os.close(fd)
        self._playing = True
        self._play_once()

    def _play_once(self):
        if not self._playing:
            return
        try:
            import winsound

            # SND_ASYNC + SND_FILENAME works (SND_ASYNC + SND_MEMORY doesn't)
            winsound.PlaySound(
                self._tmp_path,
                winsound.SND_FILENAME | winsound.SND_ASYNC | winsound.SND_NODEFAULT,
            )
        except Exception:
            return
        self._timer_id = self._root.after(self._duration_ms - 30, self._play_once)

    def stop(self):
        self._playing = False
        if self._timer_id is not None:
            self._root.after_cancel(self._timer_id)
            self._timer_id = None
        if sys.platform == "win32":
            try:
                import winsound

                winsound.PlaySound(None, 0)
            except Exception:
                pass

    def cleanup(self):
        self.stop()
        if self._tmp_path:
            try:
                os.unlink(self._tmp_path)
            except OSError:
                pass


# ---------------------------------------------------------------------------
# GUI Application  —  keygen style
# ---------------------------------------------------------------------------

MONO = ("Consolas", 10)
MONO_SM = ("Consolas", 9)
MONO_XS = ("Consolas", 8)
MONO_LG = ("Consolas", 10, "bold")


class LemonFlasherApp:
    def __init__(self, root):
        self.root = root
        self.root.title("LMN // Display Flasher")
        self.root.configure(bg=BLACK)
        self.root.resizable(False, False)

        w, h = 530, 800
        x = (root.winfo_screenwidth() - w) // 2
        y = (root.winfo_screenheight() - h) // 2
        root.geometry(f"{w}x{h}+{x}+{y}")

        # State
        self.latest_version = None
        self.asset_url = None
        self.md5 = None
        self.selected_port = tk.StringVar()
        self.is_busy = False
        self.recovery_mode = tk.BooleanVar(value=False)
        self.music = ChiptunePlayer(root)
        self.music_on = True

        # ttk style
        style = ttk.Style()
        style.theme_use("clam")
        style.configure(
            "TCombobox",
            fieldbackground=DARK,
            background=DARK,
            foreground=GREENT,
            arrowcolor=GREENT,
            bordercolor=MOON,
            selectbackground=NEBULA,
            selectforeground=BLACK,
        )
        style.map(
            "TCombobox",
            fieldbackground=[("readonly", DARK)],
            foreground=[("readonly", GREENT)],
        )

        self._build_ui()
        self.root.after(300, self._auto_init)
        # Start the chiptune!
        self.root.after(500, self.music.start)
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    def _sep(self, parent):
        tk.Frame(parent, bg=NEBULA, height=1).pack(fill="x", pady=(6, 6))

    def _build_ui(self):
        main = tk.Frame(self.root, bg=BLACK)
        main.pack(fill="both", expand=True, padx=20, pady=12)

        # ── ASCII art banner (Lemon isotipo) ──
        raw = [
            "                    ==========================",
            "               =====================================",
            "            ============================================",
            "         ===================================================",
            "       =========================================================",
            "      =============================================================",
            "     ==================================================================",
            "    ======================================================================",
            "   ==========================================================================",
            "  =============================================================================",
            "  ================================================================================",
            " ===================================================================================",
            " =====================================================================================",
            " ==========================================                 =======  ===================",
            " ======================================                                 ==================",
            "====================================                                    ===================",
            "==================================                                       ====================",
            "================================                                        ======================",
            "===============================                                          =======================",
            "===============================                                            ======================",
            "==============================                                             =======================",
            "=============================                    ==                         ======================",
            "===============================                  ====                        =======================",
            "===============================                   =====                      =======================",
            "================================                   ======                    =======================",
            "=========================  =====                     ======                  =======================",
            "========================   ======                     =======               ========================",
            "========================     =====                      =======             ========================",
            "=======================       ======                      =========        =========================",
            "=======================        ======                       ========================================",
            "=======================          ======                        =====================================",
            "=======================            =====                          ==================================",
            "=======================              ======                             ============================",
            " ======================                ====                            ============================",
            " =======================                                             ==============================",
            " =======================                                            ==============================",
            "  =======================                                          ===============================",
            "  ========================                                       ================================",
            "   ======================                                     ==================================",
            "    =====================                                   ===================================",
            "     ====================                                =====================================",
            "      ===================     ====                  =========================================",
            "       =====================================================================================",
            "        ===================================================================================",
            "          ================================================================================",
            "           =============================================================================",
            "             ==========================================================================",
            "               ======================================================================",
            "                 ==================================================================",
            "                   ==============================================================",
            "                     =========================================================",
            "                        ===================================================",
            "                           =============================================",
            "                               =====================================",
            "                                     ==========================",
        ]
        W = max(len(l) for l in raw)
        ascii_logo = "\n".join(l.center(W) for l in raw)
        tk.Label(
            main,
            text=ascii_logo,
            font=("Consolas", 4),
            bg=BLACK,
            fg=GREENT,
            justify="center",
        ).pack(pady=(2, 0))

        sub_row = tk.Frame(main, bg=BLACK)
        sub_row.pack(fill="x")
        tk.Label(
            sub_row,
            text="LMN // Display Flasher v1.0",
            font=MONO_SM,
            bg=BLACK,
            fg=NEBULA,
        ).pack(side="left", expand=True)
        self.btn_mute = tk.Button(
            sub_row,
            text="[SND:ON]",
            command=self._toggle_music,
            font=MONO_XS,
            bg=BLACK,
            fg=GREENT,
            activebackground=BLACK,
            activeforeground=NEBULA,
            bd=0,
            cursor="hand2",
        )
        self.btn_mute.pack(side="right")

        self._sep(main)

        # ── USB Port ──
        tk.Label(main, text="[ USB PORT ]", font=MONO_LG, bg=BLACK, fg=NEBULA).pack(
            anchor="w"
        )

        port_row = tk.Frame(main, bg=BLACK)
        port_row.pack(fill="x", pady=(4, 0))

        tk.Label(port_row, text="Puerto:", font=MONO_SM, bg=BLACK, fg=MOON).pack(
            side="left"
        )

        self.port_combo = ttk.Combobox(
            port_row,
            textvariable=self.selected_port,
            state="readonly",
            width=12,
            style="TCombobox",
            font=MONO_SM,
        )
        self.port_combo.pack(side="left", padx=(8, 8))

        tk.Button(
            port_row,
            text="SCAN",
            command=self._refresh_ports,
            font=MONO_XS,
            bg=DARK,
            fg=NEBULA,
            activebackground="#1A1A1A",
            activeforeground=GREENT,
            bd=1,
            relief="ridge",
            cursor="hand2",
            padx=8,
        ).pack(side="left")

        self.port_status = tk.Label(main, text="...", font=MONO_XS, bg=BLACK, fg=MOON)
        self.port_status.pack(anchor="w", pady=(2, 0))

        self._sep(main)

        # ── Firmware ──
        tk.Label(main, text="[ FIRMWARE ]", font=MONO_LG, bg=BLACK, fg=NEBULA).pack(
            anchor="w"
        )

        ver_row = tk.Frame(main, bg=BLACK)
        ver_row.pack(fill="x", pady=(4, 0))

        self.version_label = tk.Label(
            ver_row, text="Consultando...", font=MONO_SM, bg=BLACK, fg=MOON
        )
        self.version_label.pack(side="left")

        self.btn_check = tk.Button(
            ver_row,
            text="CHECK",
            command=self._check_version,
            font=MONO_XS,
            bg=DARK,
            fg=NEBULA,
            activebackground="#1A1A1A",
            activeforeground=GREENT,
            bd=1,
            relief="ridge",
            cursor="hand2",
            padx=8,
        )
        self.btn_check.pack(side="right")

        self._sep(main)

        # ── Flash ──
        self.progress_label = tk.Label(
            main, text=self._render_progress(0), font=MONO, bg=BLACK, fg=GREENT
        )
        self.progress_label.pack(fill="x")

        self.status_label = tk.Label(
            main, text="Listo", font=MONO_XS, bg=BLACK, fg=MOON
        )
        self.status_label.pack(anchor="w", pady=(2, 6))

        self.btn_flash = tk.Button(
            main,
            text="\u2588\u2588  ACTUALIZAR FIRMWARE  \u2588\u2588",
            command=self._start_flash,
            font=("Consolas", 12, "bold"),
            bg=GREENT,
            fg=BLACK,
            activebackground=EVERGREENT,
            activeforeground=BLACK,
            bd=0,
            cursor="hand2",
            pady=8,
        )
        self.btn_flash.pack(fill="x", pady=(0, 4))
        self.btn_flash.config(state="disabled")

        tk.Checkbutton(
            main,
            text="RECOVERY (borra todo + bootloader)",
            variable=self.recovery_mode,
            font=MONO_XS,
            bg=BLACK,
            fg=SOLAR,
            selectcolor=DARK,
            activebackground=BLACK,
            activeforeground=SOLAR,
            cursor="hand2",
        ).pack(anchor="w")

        self._sep(main)

        # ── Log ──
        tk.Label(main, text="[ LOG ]", font=MONO_XS, bg=BLACK, fg=MOON).pack(anchor="w")

        log_frame = tk.Frame(main, bg=DARKER, bd=1, relief="sunken")
        log_frame.pack(fill="both", expand=True, pady=(4, 0))

        self.log_text = tk.Text(
            log_frame,
            height=14,
            bg=DARKER,
            fg=GREENT,
            font=MONO_XS,
            bd=0,
            wrap="word",
            insertbackground=GREENT,
            padx=8,
            pady=6,
        )
        self.log_text.pack(fill="both", expand=True)
        self.log_text.config(state="disabled")
        self.log_text.tag_config("dim", foreground=MOON)
        self.log_text.tag_config("warn", foreground=SOLAR)
        self.log_text.tag_config("ok", foreground=GREENT)
        self.log_text.tag_config("info", foreground=NEBULA)

    # ── Progress rendering ──

    def _render_progress(self, pct):
        bar_len = 44
        filled = int(bar_len * pct / 100)
        bar = "\u2593" * filled + "\u2591" * (bar_len - filled)
        return f" [{bar}] {int(pct):3d}%"

    def _set_progress(self, pct, status=""):
        self.progress_var = pct
        self.progress_label.config(text=self._render_progress(pct))
        if status:
            self.status_label.config(text=status)

    # ── Actions ──

    def _log(self, msg, tag="ok"):
        self.log_text.config(state="normal")
        self.log_text.insert("end", f"> {msg}\n", tag)
        self.log_text.see("end")
        self.log_text.config(state="disabled")

    def _toggle_music(self):
        if self.music_on:
            self.music.stop()
            self.music_on = False
            self.btn_mute.config(text="[SND:OFF]", fg=MOON)
        else:
            self.music.start()
            self.music_on = True
            self.btn_mute.config(text="[SND:ON]", fg=GREENT)

    def _on_close(self):
        self.music.cleanup()
        self.root.destroy()

    def _auto_init(self):
        self._refresh_ports()
        self._check_version()

    def _refresh_ports(self):
        self._log("Escaneando puertos USB...", "info")
        ports, descs = detect_esp_ports()
        self.port_combo["values"] = ports
        if ports:
            self.port_combo.current(0)
            self.port_status.config(text=f"{len(ports)} dispositivo(s) USB", fg=GREENT)
            for device, desc in zip(ports, descs):
                self._log(f"  {device}: {desc}", "dim")
        else:
            self.port_status.config(text="Sin dispositivos USB", fg=SOLAR)
            self._log("Ningun dispositivo USB detectado", "warn")

    def _check_version(self):
        if self.is_busy:
            return
        self.btn_check.config(state="disabled")
        self._log("Consultando GitHub...", "info")

        def _do():
            try:
                ver, url, md5 = fetch_latest_release()
                self.root.after(0, lambda: self._on_version_found(ver, url, md5))
            except Exception as e:
                self.root.after(0, lambda: self._on_version_error(str(e)))

        threading.Thread(target=_do, daemon=True).start()

    def _on_version_found(self, ver, url, md5):
        self.latest_version = ver
        self.asset_url = url
        self.md5 = md5
        self.btn_check.config(state="normal")

        if not url:
            self.version_label.config(text=f"v{ver} - sin .bin en release", fg=SOLAR)
            self._log(f"v{ver} encontrada pero sin .bin adjunto", "warn")
            return

        self.version_label.config(text=f"v{ver} disponible", fg=GREENT)
        md5_short = md5[:8] if md5 else "?"
        self._log(f"Firmware v{ver}  MD5:{md5_short}...", "ok")
        self.btn_flash.config(state="normal")

    def _on_version_error(self, err):
        self.btn_check.config(state="normal")
        self.version_label.config(text="Error al consultar", fg=SOLAR)
        self._log(f"Error: {err}", "warn")

    def _start_flash(self):
        port = self.selected_port.get()
        if not port:
            messagebox.showwarning("Puerto", "Selecciona un puerto USB primero.")
            return
        if not self.asset_url:
            messagebox.showwarning(
                "Firmware", "Primero consulta la version disponible."
            )
            return

        ok = messagebox.askokcancel(
            "Modo Bootloader",
            "Antes de continuar, pon el dispositivo en\n"
            "modo bootloader:\n\n"
            "  1. Mantene presionado BOOT\n"
            "  2. Presiona y solta RESET\n"
            "  3. Solta BOOT\n\n"
            "Presiona OK cuando este listo.",
        )
        if not ok:
            return

        self.is_busy = True
        self.btn_flash.config(
            state="disabled", text="\u2588\u2588  FLASHEANDO...  \u2588\u2588"
        )
        self._set_progress(0, "Iniciando...")

        def _do():
            try:
                self.root.after(0, lambda: self._log("Descargando firmware...", "info"))
                self.root.after(0, lambda: self._set_progress(0, "Descargando..."))

                def dl_progress(pct):
                    self.root.after(
                        0,
                        lambda p=pct: self._set_progress(
                            p * 50, f"Descargando... {int(p * 100)}%"
                        ),
                    )

                fw = download_firmware(self.asset_url, dl_progress)
                self.root.after(
                    0, lambda: self._log(f"Descargado: {len(fw):,} bytes", "ok")
                )

                if self.md5:
                    import hashlib

                    actual = hashlib.md5(fw).hexdigest()
                    if actual != self.md5:
                        raise RuntimeError(
                            f"MD5 no coincide.\n"
                            f"Esperado: {self.md5}\n"
                            f"Recibido: {actual}"
                        )
                    self.root.after(0, lambda: self._log("MD5 verificado OK", "ok"))

                self.root.after(0, lambda: self._set_progress(55, "Flasheando..."))
                self.root.after(
                    0,
                    lambda: self._log(
                        f"Flasheando {port} @ {FLASH_BAUD} baud...", "info"
                    ),
                )

                def log_cb(msg):
                    self.root.after(0, lambda m=msg: self._log(m, "dim"))

                flash_firmware(
                    port, fw, log_cb=log_cb, recovery=self.recovery_mode.get()
                )
                self.root.after(0, self._on_flash_success)

            except Exception as e:
                self.root.after(0, lambda: self._on_flash_error(str(e)))

        threading.Thread(target=_do, daemon=True).start()

    def _on_flash_success(self):
        self.is_busy = False
        self._set_progress(100, "Completado!")
        self.progress_label.config(fg=GREENT)
        self.btn_flash.config(
            text="\u2588\u2588  ACTUALIZAR FIRMWARE  \u2588\u2588", state="normal"
        )
        self._log(f"Lemon Display actualizado a v{self.latest_version}", "ok")
        self._log("Presiona el boton RESET en el dispositivo para iniciar", "warn")
        messagebox.showinfo(
            "Listo",
            f"Firmware v{self.latest_version} instalado.\n\n"
            "Presiona el boton RESET en el dispositivo\n"
            "o desconecta y reconecta el USB.",
        )

    def _on_flash_error(self, err):
        self.is_busy = False
        self._set_progress(0, "Error")
        self.status_label.config(fg=SOLAR)
        self.btn_flash.config(
            text="\u2588\u2588  ACTUALIZAR FIRMWARE  \u2588\u2588", state="normal"
        )
        self._log(f"ERROR: {err}", "warn")
        messagebox.showerror("Error", err)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def main():
    root = tk.Tk()
    try:
        icon_data = base64.b64decode(LOGO_B64)
        icon_img = tk.PhotoImage(data=icon_data)
        root.iconphoto(True, icon_img)
    except Exception:
        pass
    LemonFlasherApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
