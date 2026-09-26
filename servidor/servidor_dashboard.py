#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Painel Meteorológico Moderno - ESP32 & Histórico HDD
- Calendário seletor de datas para navegação limpa
- Comparativo gráfico de Temperatura Interna (Azul) x Externa (Laranja)
- Design moderno minimalista sem detalhamentos desnecessários
- Temperaturas arredondadas em números inteiros
"""

import os
import sys
import json
import csv
import glob
from datetime import datetime
from collections import defaultdict
from http.server import HTTPServer, BaseHTTPRequestHandler
import urllib.request
import urllib.error

PORTA = 8089
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
CONFIG_FILE = os.path.join(SCRIPT_DIR, "config.json")

def carregar_config():
    cfg_padrao = {
        "esp32_url": "http://192.168.0.XXX/api/dados",
        "pasta_dados": os.path.join(SCRIPT_DIR, "dados")
    }
    if os.path.exists(CONFIG_FILE):
        try:
            with open(CONFIG_FILE, "r", encoding="utf-8") as f:
                cfg_padrao.update(json.load(f))
        except Exception:
            pass
    return cfg_padrao

CONFIG = carregar_config()
PASTA_DADOS = CONFIG.get("pasta_dados", os.path.join(SCRIPT_DIR, "dados"))
ESP32_URL = CONFIG.get("esp32_url", "http://192.168.0.XXX/api/dados")
ESP32_BASE = ESP32_URL.rsplit("/api/", 1)[0] if "/api/" in ESP32_URL else ESP32_URL

def obter_dados_tempo_real():
    try:
        req = urllib.request.Request(ESP32_URL, headers={"User-Agent": "Dashboard/2.1"})
        with urllib.request.urlopen(req, timeout=2.5) as response:
            if response.status == 200:
                dados = json.loads(response.read().decode("utf-8"))
                dados["online"] = True
                return dados
    except Exception:
        pass

    fallback = {
        "online": False,
        "timestamp": datetime.now().strftime("%d/%m/%Y %H:%M:%S"),
        "hora_atual": datetime.now().strftime("%H:%M:%S"),
        "data_atual": datetime.now().strftime("%d/%m/%Y"),
        "interno": {"temperatura": 0, "umidade": 0},
        "externo": {
            "local": "São Paulo",
            "temperatura": 0, "umidade": 0, "temp_max": 0, "temp_min": 0,
            "probabilidade_chuva": 0, "condicao": "Desconectado"
        },
        "historico": {
            "media_diaria": 0, "media_semanal": 0, "media_mensal": 0,
            "recap": {"data": "--", "delta_temp": 0, "temp_min": 0, "temp_max": 0, "delta_umid": 0}
        }
    }

    json_path = os.path.join(PASTA_DADOS, "resumo_recente.json")
    if os.path.exists(json_path):
        try:
            with open(json_path, "r", encoding="utf-8") as f:
                resumo = json.load(f)
                fallback["historico"]["media_diaria"] = resumo.get("media_diaria", 0)
                fallback["historico"]["media_semanal"] = resumo.get("media_semanal", 0)
                fallback["historico"]["media_mensal"] = resumo.get("media_mensal", 0)
                fallback["historico"]["recap"] = {
                    "data": resumo.get("recap_data", "--"),
                    "delta_temp": resumo.get("recap_delta_temp", 0),
                    "temp_min": resumo.get("recap_temp_min", 0),
                    "temp_max": resumo.get("recap_temp_max", 0),
                    "delta_umid": resumo.get("recap_delta_umid", 0)
                }
        except Exception:
            pass

    arquivos_csv = sorted(glob.glob(os.path.join(PASTA_DADOS, "leituras_*.csv")))
    if arquivos_csv:
        ultimo_csv = arquivos_csv[-1]
        try:
            with open(ultimo_csv, "r", encoding="utf-8") as f:
                linhas = list(csv.DictReader(f))
                if linhas:
                    ult = linhas[-1]
                    fallback["interno"]["temperatura"] = float(ult.get("Temp_Interna_C", 0))
                    fallback["interno"]["umidade"] = float(ult.get("Umid_Interna_Pct", 0))
                    if ult.get("Temp_Externa_C"):
                        fallback["externo"]["temperatura"] = float(ult.get("Temp_Externa_C", 0))
                    if ult.get("Umid_Externa_Pct"):
                        fallback["externo"]["umidade"] = float(ult.get("Umid_Externa_Pct", 0))
        except Exception:
            pass

    return fallback

def processar_historico_completo():
    arquivos = sorted(glob.glob(os.path.join(PASTA_DADOS, "leituras_*.csv")))
    dias = defaultdict(lambda: {
        "horas": {h: {"t_int": [], "u_int": [], "t_ext": []} for h in range(24)},
        "t_int_all": [],
        "u_int_all": [],
        "t_ext_all": [],
        "leituras": 0
    })

    for arq in arquivos:
        try:
            with open(arq, "r", encoding="utf-8") as f:
                reader = csv.DictReader(f)
                for r in reader:
                    dh_str = r.get("DataHora")
                    if not dh_str:
                        continue
                    try:
                        dt = datetime.strptime(dh_str, "%Y-%m-%d %H:%M:%S")
                        d_str = dt.strftime("%Y-%m-%d")
                        h = dt.hour
                        t_int = float(r["Temp_Interna_C"])
                        u_int = float(r["Umid_Interna_Pct"])

                        dias[d_str]["horas"][h]["t_int"].append(t_int)
                        dias[d_str]["horas"][h]["u_int"].append(u_int)
                        dias[d_str]["t_int_all"].append(t_int)
                        dias[d_str]["u_int_all"].append(u_int)
                        dias[d_str]["leituras"] += 1

                        if r.get("Temp_Externa_C"):
                            try:
                                t_ext = float(r["Temp_Externa_C"])
                                dias[d_str]["horas"][h]["t_ext"].append(t_ext)
                                dias[d_str]["t_ext_all"].append(t_ext)
                            except ValueError:
                                pass
                    except (ValueError, TypeError):
                        continue
        except Exception:
            pass

    resultado = {}
    for d in sorted(dias.keys(), reverse=True):
        info = dias[d]
        horas_res = []
        for h in range(24):
            h_data = info["horas"][h]
            cnt_int = len(h_data["t_int"])
            cnt_ext = len(h_data["t_ext"])
            t_ext_avg = round(sum(h_data["t_ext"]) / cnt_ext) if cnt_ext > 0 else None

            if cnt_int > 0:
                horas_res.append({
                    "hora": f"{h:02d}:00",
                    "h_num": h,
                    "t_avg": round(sum(h_data["t_int"]) / cnt_int),
                    "t_min": round(min(h_data["t_int"])),
                    "t_max": round(max(h_data["t_int"])),
                    "u_avg": round(sum(h_data["u_int"]) / cnt_int),
                    "t_ext_avg": t_ext_avg,
                    "leituras": cnt_int
                })
            else:
                horas_res.append({
                    "hora": f"{h:02d}:00",
                    "h_num": h,
                    "t_avg": None,
                    "t_min": None,
                    "t_max": None,
                    "u_avg": None,
                    "t_ext_avg": t_ext_avg,
                    "leituras": 0
                })

        t_list = info["t_int_all"]
        u_list = info["u_int_all"]
        t_ext_list = info["t_ext_all"]

        resultado[d] = {
            "data": d,
            "data_formatada": datetime.strptime(d, "%Y-%m-%d").strftime("%d/%m/%Y"),
            "leituras_total": info["leituras"],
            "t_avg": round(sum(t_list) / len(t_list)) if t_list else 0,
            "t_min": round(min(t_list)) if t_list else 0,
            "t_max": round(max(t_list)) if t_list else 0,
            "delta_t": round(max(t_list) - min(t_list)) if t_list else 0,
            "u_avg": round(sum(u_list) / len(u_list)) if u_list else 0,
            "t_ext_avg": round(sum(t_ext_list) / len(t_ext_list)) if t_ext_list else None,
            "t_ext_min": round(min(t_ext_list)) if t_ext_list else None,
            "t_ext_max": round(max(t_ext_list)) if t_ext_list else None,
            "horas": horas_res
        }

    return resultado

HTML_PAGE = r"""<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Estação Meteorológica</title>
  <style>
    :root {
      --bg: #f8fafc;
      --card-bg: #ffffff;
      --border: #e2e8f0;
      --text-main: #0f172a;
      --text-muted: #64748b;
      --blue-primary: #0284c7;
      --blue-light: #f0f9ff;
      --blue-border: #bae6fd;
      --orange-primary: #ea580c;
      --orange-light: #fff7ed;
      --orange-border: #fed7aa;
      --card-radius: 14px;
      --shadow-sm: 0 1px 3px rgba(0,0,0,0.03), 0 1px 2px rgba(0,0,0,0.02);
      --shadow-md: 0 4px 6px -1px rgba(0,0,0,0.04), 0 2px 4px -2px rgba(0,0,0,0.03);
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; }
    body { background-color: var(--bg); color: var(--text-main); padding: 2rem 1.5rem; display: flex; justify-content: center; min-height: 100vh; }
    .container { max-width: 960px; width: 100%; display: flex; flex-direction: column; gap: 1.5rem; }

    /* Cabeçalho */
    header { display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 1rem; }
    .header-left { display: flex; flex-direction: column; gap: 0.15rem; }
    .header-title { font-size: 1.35rem; font-weight: 800; letter-spacing: -0.03em; color: var(--text-main); }
    .header-sub { font-size: 0.85rem; font-weight: 500; color: var(--text-muted); }

    .header-right { display: flex; align-items: center; gap: 0.75rem; }
    .live-dot-wrap { display: flex; align-items: center; gap: 0.4rem; font-size: 0.75rem; font-weight: 700; color: #16a34a; background: #f0fdf4; border: 1px solid #bbf7d0; padding: 0.35rem 0.7rem; border-radius: 999px; }
    .live-dot { width: 7px; height: 7px; background: #22c55e; border-radius: 50%; box-shadow: 0 0 0 2px rgba(34,197,94,0.25); }
    .clock-display { font-size: 0.95rem; font-weight: 700; color: var(--text-main); font-variant-numeric: tabular-nums; background: #ffffff; border: 1px solid var(--border); padding: 0.35rem 0.8rem; border-radius: 999px; box-shadow: var(--shadow-sm); }

    /* Abas Pílula */
    .tabs-nav { display: flex; gap: 0.4rem; background: #ffffff; padding: 0.35rem; border-radius: 12px; border: 1px solid var(--border); box-shadow: var(--shadow-sm); }
    .tab-btn { flex: 1; text-align: center; background: transparent; border: none; padding: 0.65rem 1rem; font-size: 0.85rem; font-weight: 700; color: var(--text-muted); cursor: pointer; border-radius: 9px; transition: all 0.18s ease; }
    .tab-btn:hover { color: var(--text-main); background: var(--bg); }
    .tab-btn.active { background: var(--blue-primary); color: #ffffff; box-shadow: 0 2px 4px rgba(2,132,199,0.25); }

    .tab-pane { display: none; flex-direction: column; gap: 1.25rem; animation: slideUp 0.18s ease-out; }
    .tab-pane.active { display: flex; }
    @keyframes slideUp { from { opacity: 0; transform: translateY(4px); } to { opacity: 1; transform: translateY(0); } }

    /* Cards */
    .card { background: var(--card-bg); border: 1px solid var(--border); border-radius: var(--card-radius); padding: 1.35rem; box-shadow: var(--shadow-sm); }
    .card-label { font-size: 0.75rem; font-weight: 700; color: var(--text-muted); text-transform: uppercase; letter-spacing: 0.08em; margin-bottom: 0.85rem; }

    .grid-2 { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 1.25rem; }
    .grid-3 { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 1rem; }
    .grid-4 { display: grid; grid-template-columns: repeat(auto-fit, minmax(130px, 1fr)); gap: 0.75rem; }

    /* Métricas */
    .metric-main { display: flex; align-items: baseline; gap: 0.15rem; }
    .temp-val { font-size: 3.5rem; font-weight: 800; color: var(--text-main); line-height: 1; letter-spacing: -0.03em; }
    .unit { font-size: 1.75rem; font-weight: 600; color: var(--text-muted); margin-left: 0.1rem; }
    .hum-val { font-size: 1rem; font-weight: 600; color: var(--blue-primary); margin-top: 0.5rem; }

    .info-footer { display: flex; justify-content: space-between; align-items: center; margin-top: 1rem; padding-top: 0.85rem; border-top: 1px solid var(--border); font-size: 0.8rem; }
    .info-pill { background: var(--blue-light); color: var(--blue-primary); font-weight: 700; padding: 0.25rem 0.6rem; border-radius: 6px; }

    /* Banner Comparativo */
    .diff-banner { background: var(--blue-light); border: 1px solid var(--blue-border); border-radius: 10px; padding: 0.9rem 1.25rem; font-size: 0.9rem; font-weight: 600; color: #0369a1; text-align: center; }

    .stat-card { background: #ffffff; border: 1px solid var(--border); border-radius: var(--card-radius); padding: 1.25rem; text-align: center; box-shadow: var(--shadow-sm); }
    .stat-card .label { font-size: 0.75rem; font-weight: 700; color: var(--text-muted); text-transform: uppercase; letter-spacing: 0.05em; }
    .stat-card .val { font-size: 2.25rem; font-weight: 800; color: var(--blue-primary); margin-top: 0.35rem; line-height: 1; }

    .recap-box { background: #ffffff; border: 1px solid var(--blue-border); border-left: 4px solid var(--blue-primary); border-radius: var(--card-radius); padding: 1.15rem 1.35rem; box-shadow: var(--shadow-sm); }
    .recap-title { font-size: 0.75rem; font-weight: 800; text-transform: uppercase; color: var(--blue-primary); letter-spacing: 0.06em; }
    .recap-content { font-size: 0.95rem; color: var(--text-main); margin-top: 0.4rem; line-height: 1.5; font-weight: 500; }

    /* Barra de Seleção por Calendário */
    .calendar-bar { display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 0.75rem; background: #ffffff; border: 1px solid var(--border); padding: 0.85rem 1.25rem; border-radius: var(--card-radius); box-shadow: var(--shadow-sm); }
    .calendar-control-group { display: flex; align-items: center; gap: 0.5rem; }
    .btn-nav-date { background: #ffffff; border: 1px solid var(--border); padding: 0.45rem 0.75rem; border-radius: 8px; font-weight: 800; font-size: 0.9rem; cursor: pointer; color: var(--text-main); transition: all 0.15s; }
    .btn-nav-date:hover { background: var(--blue-light); border-color: var(--blue-border); color: var(--blue-primary); }
    
    .date-display-box { position: relative; display: flex; align-items: center; gap: 0.55rem; background: #ffffff; border: 1px solid var(--border); border-radius: 8px; padding: 0.45rem 0.95rem; font-size: 0.95rem; font-weight: 800; color: var(--text-main); cursor: pointer; transition: all 0.15s; user-select: none; }
    .date-display-box:hover { border-color: var(--blue-primary); background: var(--blue-light); color: var(--blue-primary); }
    .hidden-date-input { position: absolute; left: 0; top: 0; width: 100%; height: 100%; opacity: 0; cursor: pointer; }

    .btn-today { background: var(--blue-light); border: 1px solid var(--blue-border); color: var(--blue-primary); font-size: 0.85rem; font-weight: 700; padding: 0.45rem 0.9rem; border-radius: 8px; cursor: pointer; }
    .btn-today:hover { background: var(--blue-primary); color: #ffffff; }

    .date-badge { font-size: 0.8rem; font-weight: 700; color: var(--text-muted); background: var(--bg); padding: 0.4rem 0.75rem; border-radius: 6px; }

    /* Gráfico com Comparativo Interno x Externo */
    .chart-box { background: #ffffff; border: 1px solid var(--border); border-radius: var(--card-radius); padding: 1.25rem; box-shadow: var(--shadow-sm); }
    .chart-top-bar { display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 0.75rem; margin-bottom: 0.85rem; }
    .chart-title { font-size: 0.8rem; font-weight: 700; text-transform: uppercase; color: var(--text-muted); letter-spacing: 0.05em; }
    .chart-legend { display: flex; align-items: center; gap: 1.25rem; font-size: 0.8rem; font-weight: 700; }
    .legend-item { display: flex; align-items: center; gap: 0.4rem; }
    .legend-indicator { width: 12px; height: 12px; border-radius: 3px; }

    /* Matriz de 24 Horas */
    .hours-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(105px, 1fr)); gap: 0.6rem; }
    .hour-box { border: 1px solid var(--border); border-radius: 10px; padding: 0.65rem 0.45rem; text-align: center; background: #ffffff; transition: transform 0.12s; }
    .hour-box:hover { transform: translateY(-2px); border-color: var(--blue-border); }
    .hour-box.has-data { background: linear-gradient(180deg, #f0f9ff 0%, #ffffff 100%); border-color: #bae6fd; }
    .hour-box.no-data { opacity: 0.4; background: #f8fafc; border-style: dashed; }
    .hb-time { font-size: 0.75rem; font-weight: 700; color: var(--text-muted); }
    .hb-temps-row { display: flex; justify-content: center; align-items: baseline; gap: 0.35rem; margin: 0.25rem 0; }
    .hb-temp-int { font-size: 1.25rem; font-weight: 800; color: var(--blue-primary); line-height: 1; }
    .hb-temp-ext { font-size: 0.9rem; font-weight: 700; color: var(--orange-primary); }
    .hb-hum { font-size: 0.7rem; font-weight: 600; color: var(--text-muted); }

    /* Formulários */
    .form-group { display: flex; flex-direction: column; gap: 0.35rem; }
    label { font-size: 0.75rem; font-weight: 700; color: var(--text-muted); text-transform: uppercase; letter-spacing: 0.04em; }
    input[type="text"], input[type="number"] { width: 100%; border: 1px solid var(--border); border-radius: 8px; padding: 0.65rem 0.85rem; font-size: 0.95rem; color: var(--text-main); background: #ffffff; }
    input:focus { outline: none; border-color: var(--blue-primary); }
    button { cursor: pointer; border: 1px solid transparent; border-radius: 8px; padding: 0.65rem 1.25rem; font-size: 0.85rem; font-weight: 700; text-transform: uppercase; letter-spacing: 0.04em; transition: opacity 0.15s; }
    button:hover { opacity: 0.9; }
    .btn-primary { background: var(--blue-primary); color: #ffffff; }
    .btn-secondary { background: var(--blue-light); color: var(--blue-primary); border-color: var(--blue-border); }
  </style>
</head>
<body>
  <div class="container">
    <!-- Cabeçalho -->
    <header>
      <div class="header-left">
        <div class="header-title">Estação Meteorológica</div>
      </div>
      <div class="header-right">
        <div class="live-dot-wrap">
          <div class="live-dot"></div>
          <span id="status-text">AO VIVO</span>
        </div>
        <div class="clock-display" id="relogio">--:--:--</div>
      </div>
    </header>

    <!-- Navegação em Abas -->
    <div class="tabs-nav">
      <button class="tab-btn active" onclick="trocarAba('tab-principal')">Painel Principal</button>
      <button class="tab-btn" onclick="trocarAba('tab-historico')">Histórico 24h</button>
      <button class="tab-btn" onclick="trocarAba('tab-alexa')">Alexa & Notificações</button>
      <button class="tab-btn" onclick="trocarAba('tab-config')">Configurações</button>
    </div>

    <!-- ================================================================= -->
    <!-- ABA 1: PAINEL PRINCIPAL -->
    <!-- ================================================================= -->
    <div id="tab-principal" class="tab-pane active">
      <div class="grid-2">
        <!-- Sensor Interno -->
        <div class="card">
          <div class="card-label"><span>Ambiente Interno</span></div>
          <div class="metric-main">
            <div class="temp-val" id="temp-int" style="color:var(--blue-primary);">--</div>
            <div class="unit">°C</div>
          </div>
          <div class="hum-val" id="umid-int">Umidade: -- %</div>
          <div class="info-footer">
            <span style="color:var(--text-muted); font-weight:600;">Sensação Térmica</span>
            <span class="info-pill" id="conforto">--</span>
          </div>
        </div>

        <!-- Sensor Externo -->
        <div class="card">
          <div class="card-label"><span>Ambiente Externo</span></div>
          <div class="metric-main">
            <div class="temp-val" id="temp-ext" style="color:var(--orange-primary);">--</div>
            <div class="unit">°C</div>
          </div>
          <div class="hum-val" id="umid-ext" style="color:var(--orange-primary);">Umidade: -- %</div>
          <div class="info-footer">
            <span style="color:var(--text-muted); font-weight:600;" id="previsao-min-max">--</span>
            <span class="info-pill" id="prob-chuva" style="background:var(--orange-light); color:var(--orange-primary);">Chuva 0%</span>
          </div>
        </div>
      </div>

      <!-- Banner Comparativo -->
      <div class="diff-banner" id="diff-banner">
        Calculando comparativo entre os ambientes...
      </div>

      <!-- Médias Consolidadas -->
      <div class="grid-3">
        <div class="stat-card">
          <div class="label">Média Hoje</div>
          <div class="val" id="med-hoje">--°</div>
        </div>
        <div class="stat-card">
          <div class="label">Média Semanal</div>
          <div class="val" id="med-sem">--°</div>
        </div>
        <div class="stat-card">
          <div class="label">Média Mensal</div>
          <div class="val" id="med-mes">--°</div>
        </div>
      </div>

      <!-- Recap de Maior Variação -->
      <div class="recap-box">
        <div class="recap-title">Recap: Maior Oscilação Térmica</div>
        <div class="recap-content" id="recap-texto">
          Carregando dados históricos...
        </div>
      </div>
    </div>

    <!-- ================================================================= -->
    <!-- ABA 2: HISTÓRICO 24H COM CALENDÁRIO E GRÁFICO DUPLO -->
    <!-- ================================================================= -->
    <div id="tab-historico" class="tab-pane">
      <!-- Seletor de Data por Calendário -->
      <div class="calendar-bar">
        <div class="calendar-control-group">
          <button type="button" class="btn-nav-date" onclick="navegarData(-1)" title="Dia Anterior">&larr;</button>
          <div class="date-display-box" onclick="abrirCalendario()" title="Clique para abrir o calendário">
            <span style="font-size:1.05rem; line-height:1;">📅</span>
            <span id="data-formatada-br">--/--/----</span>
            <input type="date" id="data-calendario" class="hidden-date-input" onchange="selecionarDataCalendario(this.value)">
          </div>
          <button type="button" class="btn-nav-date" onclick="navegarData(1)" title="Próximo Dia">&rarr;</button>
        </div>
        <div style="display:flex; align-items:center; gap:0.6rem;">
          <button type="button" class="btn-today" onclick="selecionarHoje()">Hoje</button>
          <div id="data-info-badge" class="date-badge">Carregando...</div>
        </div>
      </div>

      <!-- Resumo do Dia Selecionado -->
      <div class="grid-4">
        <div class="stat-card" style="padding:1rem;">
          <div class="label">Média Interna</div>
          <div class="val" id="d-avg" style="font-size:1.6rem; color:var(--blue-primary);">--°</div>
        </div>
        <div class="stat-card" style="padding:1rem;">
          <div class="label">Média Externa</div>
          <div class="val" id="d-ext-avg" style="font-size:1.6rem; color:var(--orange-primary);">--°</div>
        </div>
        <div class="stat-card" style="padding:1rem;">
          <div class="label">Mín / Máx Interna</div>
          <div class="val" id="d-min-max" style="font-size:1.4rem; color:var(--text-main);">--° / --°</div>
        </div>
        <div class="stat-card" style="padding:1rem;">
          <div class="label">Variação Interna</div>
          <div class="val" id="d-delta" style="font-size:1.6rem; color:#7c3aed;">--°</div>
        </div>
        <div class="stat-card" style="padding:1rem;">
          <div class="label">Umidade Média</div>
          <div class="val" id="d-uavg" style="font-size:1.6rem; color:#0891b2;">--%</div>
        </div>
      </div>

      <!-- Gráfico SVG com Comparativo Interna x Externa -->
      <div class="chart-box">
        <div class="chart-top-bar">
          <div class="chart-title" id="chart-title">Comparativo Térmico 24 Horas</div>
          <div class="chart-legend">
            <div class="legend-item">
              <div class="legend-indicator" style="background:var(--blue-primary);"></div>
              <span>Temp. Interna</span>
            </div>
            <div class="legend-item" id="legend-ext-item">
              <div class="legend-indicator" style="background:var(--orange-primary);"></div>
              <span>Temp. Externa</span>
            </div>
          </div>
        </div>
        <div id="svg-chart-wrapper" style="width:100%; height:200px; position:relative;">
          <svg id="svg-chart" width="100%" height="200" preserveAspectRatio="none" style="overflow:visible;"></svg>
          <div id="chart-tooltip" style="display:none; position:absolute; background:#0f172a; color:#ffffff; padding:0.4rem 0.75rem; border-radius:6px; font-size:0.75rem; pointer-events:none; box-shadow:0 4px 6px rgba(0,0,0,0.15); z-index:10; white-space:nowrap;"></div>
        </div>
      </div>

      <!-- Matriz 24 Horas -->
      <div class="card">
        <div class="card-label">
          <span>Detalhamento Hora a Hora (00:00 às 23:00)</span>
        </div>
        <div class="hours-grid" id="hours-grid"></div>
      </div>
    </div>

    <!-- ================================================================= -->
    <!-- ABA 3: ALEXA & NOTIFICAÇÕES -->
    <!-- ================================================================= -->
    <div id="tab-alexa" class="tab-pane">
      <div class="card">
        <div class="card-label"><span>Comando de Voz para Alexa</span></div>
        <div style="display:flex; flex-direction:column; gap:0.85rem;">
          <div class="form-group">
            <label>Frase Personalizada:</label>
            <input type="text" id="alexa-input" placeholder="Digite uma frase (ou deixe em branco para o resumo do dia)...">
          </div>
          <div style="display:flex; gap:0.75rem; flex-wrap:wrap;">
            <button type="button" class="btn-primary" onclick="dispararAlexa()">Falar na Alexa</button>
            <button type="button" class="btn-secondary" onclick="dispararNtfy()">Testar Alerta Celular</button>
          </div>
          <div id="alexa-status" style="font-size:0.85rem; font-weight:600; display:none; padding:0.6rem; border-radius:8px;"></div>
        </div>
      </div>
    </div>

    <!-- ================================================================= -->
    <!-- ABA 4: CONFIGURAÇÕES -->
    <!-- ================================================================= -->
    <div id="tab-config" class="tab-pane">
      <!-- Telemetria e Monitoramento do ESP32 -->
      <div class="card">
        <div class="card-label"><span>Diagnóstico & Telemetria do ESP32</span></div>
        <div class="grid-4">
          <div class="stat-card" style="padding:1rem;">
            <div class="label">Temperatura do Chip</div>
            <div class="val" id="sys-temp-chip" style="font-size:1.6rem; color:#dc2626;">-- C</div>
          </div>
          <div class="stat-card" style="padding:1rem;">
            <div class="label">Memória RAM Livre</div>
            <div class="val" id="sys-ram" style="font-size:1.6rem; color:var(--blue-primary);">-- KB</div>
          </div>
          <div class="stat-card" style="padding:1rem;">
            <div class="label">Sinal Wi-Fi</div>
            <div class="val" id="sys-wifi" style="font-size:1.6rem; color:#16a34a;">-- dBm</div>
          </div>
          <div class="stat-card" style="padding:1rem;">
            <div class="label">Tempo Ativo</div>
            <div class="val" id="sys-uptime" style="font-size:1.4rem; color:var(--text-main);">--</div>
          </div>
        </div>
      </div>

      <div class="card">
        <div class="card-label"><span>Configurações do Dispositivo</span></div>
        <form id="form-config" onsubmit="salvarConfig(event)">
          <div class="grid-2">
            <div class="form-group">
              <label>CEP:</label>
              <input type="text" id="cfg-cep" name="cep" placeholder="01136000" maxlength="9">
            </div>
            <div class="form-group">
              <label>Horário do Aviso Diário:</label>
              <div style="display:flex; align-items:center; gap:0.5rem;">
                <input type="number" id="cfg-hora" name="hora" min="0" max="23" style="width:75px; text-align:center;">
                <span style="font-weight:700;">:</span>
                <input type="number" id="cfg-minuto" name="minuto" min="0" max="59" style="width:75px; text-align:center;">
              </div>
            </div>
          </div>

          <div style="display:flex; align-items:center; gap:0.5rem; margin-top:1rem;">
            <input type="checkbox" id="cfg-ativo" name="ativo" style="width:16px; height:16px;">
            <label for="cfg-ativo" style="cursor:pointer; margin:0;">Ativar envio automático de avisos</label>
          </div>

          <div class="grid-2" style="margin-top:1rem;">
            <div class="form-group">
              <label>Tópico NTFY:</label>
              <input type="text" id="cfg-ntfy" name="ntfy">
            </div>
            <div class="form-group">
              <label>Dispositivo Alexa:</label>
              <input type="text" id="cfg-device" name="vm_device">
            </div>
          </div>

          <div class="form-group" style="margin-top:1rem;">
            <label>Token Voice Monkey:</label>
            <input type="text" id="cfg-token" name="vm_token">
          </div>

          <div style="margin-top:1.25rem;">
            <button type="submit" class="btn-primary">Salvar</button>
          </div>
        </form>
      </div>
    </div>
  </div>

  <script>
    let dadosAoVivo = {};
    let historicoCompleto = {};
    let datasDisponiveis = [];
    let diaSelecionado = null;

    function trocarAba(abaId) {
      document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
      document.querySelectorAll('.tab-pane').forEach(p => p.classList.remove('active'));
      const btn = Array.from(document.querySelectorAll('.tab-btn')).find(b => b.getAttribute('onclick').includes(abaId));
      if (btn) btn.classList.add('active');
      const pane = document.getElementById(abaId);
      if (pane) pane.classList.add('active');

      if (abaId === 'tab-historico' && !diaSelecionado && datasDisponiveis.length > 0) {
        selecionarDataCalendario(datasDisponiveis[0]);
      }
    }

    setInterval(() => {
      const d = new Date();
      document.getElementById('relogio').innerText = d.toLocaleTimeString('pt-BR');
    }, 1000);

    async function carregarDadosAoVivo() {
      try {
        const res = await fetch('/api/dados');
        if (!res.ok) return;
        const d = await res.json();
        dadosAoVivo = d;


        if (d.interno) {
          const tInt = Math.round(d.interno.temperatura);
          const uInt = Math.round(d.interno.umidade);
          document.getElementById('temp-int').innerText = tInt;
          document.getElementById('umid-int').innerText = 'Umidade: ' + uInt + ' %';
          let c = 'Agradável';
          if (tInt >= 28) c = 'Calor';
          else if (tInt <= 18) c = 'Frio';
          document.getElementById('conforto').innerText = c;
        }

        if (d.externo) {
          const tExt = Math.round(d.externo.temperatura);
          const uExt = Math.round(d.externo.umidade);
          document.getElementById('temp-ext').innerText = tExt;
          document.getElementById('umid-ext').innerText = 'Umidade: ' + uExt + ' % • ' + (d.externo.condicao || '');
          document.getElementById('previsao-min-max').innerText = 'Mín ' + Math.round(d.externo.temp_min) + '° / Máx ' + Math.round(d.externo.temp_max) + '°';
          document.getElementById('prob-chuva').innerText = 'Chuva ' + (d.externo.probabilidade_chuva || 0) + '%';
        }

        if (d.interno && d.externo && d.externo.temperatura > 0) {
          const tInt = Math.round(d.interno.temperatura);
          const tExt = Math.round(d.externo.temperatura);
          const diff = tInt - tExt;
          const absDiff = Math.abs(diff);
          const banner = document.getElementById('diff-banner');
          if (diff > 0) {
            banner.innerText = `O ambiente interno está ${absDiff}° mais quente que o exterior.`;
          } else if (diff < 0) {
            banner.innerText = `O ambiente interno está ${absDiff}° mais fresco que o exterior.`;
          } else {
            banner.innerText = `As temperaturas interna e externa estão iguais.`;
          }
        }

        if (d.historico) {
          const h = d.historico;
          if (h.media_diaria > 0) document.getElementById('med-hoje').innerText = Math.round(h.media_diaria) + '°';
          if (h.media_semanal > 0) document.getElementById('med-sem').innerText = Math.round(h.media_semanal) + '°';
          if (h.media_mensal > 0) document.getElementById('med-mes').innerText = Math.round(h.media_mensal) + '°';

          const r = h.recap;
          if (r && r.data && r.data !== '--') {
            const pR = r.data.split('-');
            const dataRecapBR = (pR.length === 3) ? `${pR[2]}/${pR[1]}/${pR[0]}` : r.data;
            document.getElementById('recap-texto').innerHTML = 
              `No dia <b>${dataRecapBR}</b> ocorreu a maior oscilação térmica: ` +
              `variação de <b>${Math.round(r.delta_temp)}°</b> (${Math.round(r.temp_min)}° a ${Math.round(r.temp_max)}°) ` +
              `e amplitude de umidade de <b>${Math.round(r.delta_umid)}%</b>.`;
          }
        }

        if (d.sistema) {
          if (d.sistema.temp_chip !== undefined) document.getElementById('sys-temp-chip').innerText = Math.round(d.sistema.temp_chip) + ' C';
          if (d.sistema.ram_livre_kb !== undefined) document.getElementById('sys-ram').innerText = d.sistema.ram_livre_kb + ' KB';
          if (d.sistema.sinal_wifi_dbm !== undefined) document.getElementById('sys-wifi').innerText = d.sistema.sinal_wifi_dbm + ' dBm';
          if (d.sistema.uptime_segundos !== undefined) {
            const s = d.sistema.uptime_segundos;
            const h = Math.floor(s / 3600);
            const m = Math.floor((s % 3600) / 60);
            document.getElementById('sys-uptime').innerText = `${h}h ${m}m`;
          }
        }
      } catch (e) {
        console.error("Erro dados ao vivo:", e);
      }
    }

    function abrirCalendario() {
      const el = document.getElementById('data-calendario');
      if (el.showPicker) {
        el.showPicker();
      } else {
        el.focus();
      }
    }

    async function carregarHistorico() {
      try {
        const res = await fetch('/api/historico');
        if (!res.ok) return;
        historicoCompleto = await res.json();
        datasDisponiveis = Object.keys(historicoCompleto).sort(); // Ordenado ascendente

        if (datasDisponiveis.length > 0) {
          const calInput = document.getElementById('data-calendario');
          calInput.min = datasDisponiveis[0];
          calInput.max = datasDisponiveis[datasDisponiveis.length - 1];

          // Seleciona a data mais recente por padrão
          const dataMaisRecente = datasDisponiveis[datasDisponiveis.length - 1];
          selecionarDataCalendario(dataMaisRecente);
        }
      } catch (e) {
        console.error("Erro histórico:", e);
      }
    }

    function selecionarDataCalendario(dataStr) {
      if (!dataStr) return;
      diaSelecionado = dataStr;
      document.getElementById('data-calendario').value = dataStr;
      const partes = dataStr.split('-');
      const dataBR = (partes.length === 3) ? `${partes[2]}/${partes[1]}/${partes[0]}` : dataStr;
      document.getElementById('data-formatada-br').innerText = dataBR;

      const badge = document.getElementById('data-info-badge');
      const d = historicoCompleto[dataStr];

      if (!d) {
        badge.innerText = 'Sem registros nesta data';
        badge.style.color = '#ef4444';
        document.getElementById('d-avg').innerText = '--°';
        document.getElementById('d-ext-avg').innerText = '--°';
        document.getElementById('d-min-max').innerText = '--° / --°';
        document.getElementById('d-delta').innerText = '--°';
        document.getElementById('d-uavg').innerText = '--%';
        document.getElementById('hours-grid').innerHTML = '<div style="grid-column:1/-1; padding:2rem; text-align:center; color:#94a3b8;">Nenhuma leitura foi gravada para este dia.</div>';
        document.getElementById('svg-chart').innerHTML = '<text x="50%" y="50%" text-anchor="middle" fill="#94a3b8" font-size="12">Sem dados registrados.</text>';
        return;
      }

      badge.innerText = `${d.leituras_total} leituras salvas`;
      badge.style.color = 'var(--text-muted)';

      document.getElementById('d-avg').innerText = Math.round(d.t_avg) + '°';
      document.getElementById('d-ext-avg').innerText = d.t_ext_avg !== null ? Math.round(d.t_ext_avg) + '°' : '--°';
      document.getElementById('d-min-max').innerText = `${Math.round(d.t_min)}° / ${Math.round(d.t_max)}°`;
      document.getElementById('d-delta').innerText = Math.round(d.delta_t) + '°';
      document.getElementById('d-uavg').innerText = Math.round(d.u_avg) + '%';

      // Matriz 24 Horas
      const grid = document.getElementById('hours-grid');
      grid.innerHTML = '';

      d.horas.forEach(h => {
        const box = document.createElement('div');
        const hasData = h.leituras > 0;
        box.className = `hour-box ${hasData ? 'has-data' : 'no-data'}`;
        const extHtml = h.t_ext_avg !== null ? `<span class="hb-temp-ext">${h.t_ext_avg}°</span>` : '';
        box.innerHTML = `
          <div class="hb-time">${h.hora}</div>
          <div class="hb-temps-row">
            <span class="hb-temp-int">${hasData ? h.t_avg + '°' : '--'}</span>
            ${extHtml}
          </div>
          <div class="hb-hum">${hasData ? h.u_avg + '%' : '--'}</div>
        `;
        grid.appendChild(box);
      });

      renderizarGraficoComparativo(d.horas, d.data_formatada);
    }

    function navegarData(direcao) {
      if (!diaSelecionado || datasDisponiveis.length === 0) return;
      const idx = datasDisponiveis.indexOf(diaSelecionado);
      if (idx !== -1) {
        const novoIdx = idx + direcao;
        if (novoIdx >= 0 && novoIdx < datasDisponiveis.length) {
          selecionarDataCalendario(datasDisponiveis[novoIdx]);
          return;
        }
      }
      // Se a data selecionada não está na lista ou navegou além dos limites
      const dAtual = new Date(diaSelecionado + 'T12:00:00');
      dAtual.setDate(dAtual.getDate() + direcao);
      const iso = dAtual.toISOString().split('T')[0];
      selecionarDataCalendario(iso);
    }

    function selecionarHoje() {
      if (datasDisponiveis.length > 0) {
        selecionarDataCalendario(datasDisponiveis[datasDisponiveis.length - 1]);
      }
    }

    function renderizarGraficoComparativo(horas, dataFormatada) {
      const svg = document.getElementById('svg-chart');
      document.getElementById('chart-title').innerText = `Curva Térmica: Interna x Externa - ${dataFormatada}`;

      const pontosInt = horas.filter(h => h.leituras > 0);
      const pontosExt = horas.filter(h => h.t_ext_avg !== null);

      if (pontosInt.length === 0) {
        svg.innerHTML = '<text x="50%" y="50%" text-anchor="middle" fill="#94a3b8" font-size="12">Sem dados registrados.</text>';
        return;
      }

      // Legenda Externa visível apenas se houver pontos externos
      document.getElementById('legend-ext-item').style.display = pontosExt.length > 0 ? 'flex' : 'none';

      const todasTemps = [...pontosInt.map(p => p.t_min), ...pontosInt.map(p => p.t_max), ...pontosExt.map(p => p.t_ext_avg)];
      const minT = Math.floor(Math.min(...todasTemps)) - 1;
      const maxT = Math.ceil(Math.max(...todasTemps)) + 1;
      const tRange = (maxT - minT) || 1;

      const W = 900;
      const H = 200;
      const padL = 35;
      const padR = 20;
      const padT = 20;
      const padB = 25;

      const chartW = W - padL - padR;
      const chartH = H - padT - padB;

      function getX(hNum) { return padL + (hNum / 23) * chartW; }
      function getY(temp) { return padT + chartH - ((temp - minT) / tRange) * chartH; }

      let svgHTML = `
        <defs>
          <linearGradient id="gradInt" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stop-color="#0284c7" stop-opacity="0.18"/>
            <stop offset="100%" stop-color="#0284c7" stop-opacity="0.01"/>
          </linearGradient>
          <linearGradient id="gradExt" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stop-color="#ea580c" stop-opacity="0.12"/>
            <stop offset="100%" stop-color="#ea580c" stop-opacity="0.01"/>
          </linearGradient>
        </defs>
      `;

      // Linhas Horizontais
      const steps = 3;
      for (let i = 0; i <= steps; i++) {
        const val = Math.round(minT + (tRange * i) / steps);
        const y = getY(val);
        svgHTML += `
          <line x1="${padL}" y1="${y}" x2="${W - padR}" y2="${y}" stroke="#f1f5f9" stroke-width="1"/>
          <text x="${padL - 8}" y="${y + 4}" fill="#94a3b8" font-size="10" text-anchor="end" font-weight="600">${val}°</text>
        `;
      }

      // Rótulos de Horas
      [0, 4, 8, 12, 16, 20, 23].forEach(h => {
        const x = getX(h);
        svgHTML += `<text x="${x}" y="${H - 5}" fill="#94a3b8" font-size="10" text-anchor="middle" font-weight="600">${String(h).padStart(2,'0')}h</text>`;
      });

      // 1. Curva Externa (Laranja) se disponível
      if (pontosExt.length > 0) {
        let pathExt = '';
        let areaExt = '';
        pontosExt.forEach((p, idx) => {
          const x = getX(p.h_num);
          const y = getY(p.t_ext_avg);
          if (idx === 0) {
            pathExt += `M ${x} ${y}`;
            areaExt += `M ${x} ${H - padB} L ${x} ${y}`;
          } else {
            pathExt += ` L ${x} ${y}`;
            areaExt += ` L ${x} ${y}`;
          }
        });
        const ultExt = pontosExt[pontosExt.length - 1];
        areaExt += ` L ${getX(ultExt.h_num)} ${H - padB} Z`;

        svgHTML += `<path d="${areaExt}" fill="url(#gradExt)"/>`;
        svgHTML += `<path d="${pathExt}" fill="none" stroke="#ea580c" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"/>`;

        pontosExt.forEach(p => {
          const x = getX(p.h_num);
          const y = getY(p.t_ext_avg);
          svgHTML += `<circle cx="${x}" cy="${y}" r="3.5" fill="#ffffff" stroke="#ea580c" stroke-width="2"/>`;
        });
      }

      // 2. Curva Interna (Azul)
      let pathInt = '';
      let areaInt = '';
      pontosInt.forEach((p, idx) => {
        const x = getX(p.h_num);
        const y = getY(p.t_avg);
        if (idx === 0) {
          pathInt += `M ${x} ${y}`;
          areaInt += `M ${x} ${H - padB} L ${x} ${y}`;
        } else {
          pathInt += ` L ${x} ${y}`;
          areaInt += ` L ${x} ${y}`;
        }
      });
      const ultInt = pontosInt[pontosInt.length - 1];
      areaInt += ` L ${getX(ultInt.h_num)} ${H - padB} Z`;

      svgHTML += `<path d="${areaInt}" fill="url(#gradInt)"/>`;
      svgHTML += `<path d="${pathInt}" fill="none" stroke="#0284c7" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"/>`;

      pontosInt.forEach(p => {
        const x = getX(p.h_num);
        const y = getY(p.t_avg);
        svgHTML += `
          <circle cx="${x}" cy="${y}" r="4" fill="#ffffff" stroke="#0284c7" stroke-width="2" class="chart-dot"
            data-hora="${p.hora}" data-int="${p.t_avg}" data-ext="${p.t_ext_avg !== null ? p.t_ext_avg : '--'}" data-hum="${p.u_avg}"/>
        `;
      });

      svg.setAttribute('viewBox', `0 0 ${W} ${H}`);
      svg.innerHTML = svgHTML;

      // Tooltips Interativos
      const tooltip = document.getElementById('chart-tooltip');
      document.querySelectorAll('.chart-dot').forEach(dot => {
        dot.addEventListener('mouseenter', (e) => {
          const hora = dot.getAttribute('data-hora');
          const tI = dot.getAttribute('data-int');
          const tE = dot.getAttribute('data-ext');
          const hum = dot.getAttribute('data-hum');
          const extTxt = tE !== '--' ? ` • Externa: <span style="color:#fdba74;">${tE}°</span>` : '';
          tooltip.innerHTML = `<b>${hora}</b> — Interna: <span style="color:#38bdf8;">${tI}°</span>${extTxt} (${hum}%)`;
          tooltip.style.display = 'block';
        });
        dot.addEventListener('mousemove', (e) => {
          const rect = document.getElementById('svg-chart-wrapper').getBoundingClientRect();
          tooltip.style.left = (e.clientX - rect.left + 10) + 'px';
          tooltip.style.top = (e.clientY - rect.top - 30) + 'px';
        });
        dot.addEventListener('mouseleave', () => {
          tooltip.style.display = 'none';
        });
      });
    }

    async function dispararAlexa() {
      const texto = document.getElementById('alexa-input').value;
      const statusDiv = document.getElementById('alexa-status');
      statusDiv.style.display = 'block';
      statusDiv.style.background = '#f0f9ff';
      statusDiv.style.color = '#0284c7';
      statusDiv.innerText = 'Enviando comando para a Alexa...';

      try {
        const res = await fetch('/api/falar-alexa', {
          method: 'POST',
          headers: {'Content-Type': 'application/json'},
          body: JSON.stringify({ texto: texto })
        });
        const d = await res.json();
        statusDiv.style.background = '#dcfce7';
        statusDiv.style.color = '#15803d';
        statusDiv.innerText = d.mensagem || 'Mensagem enviada para a Alexa!';
      } catch (e) {
        statusDiv.style.background = '#fee2e2';
        statusDiv.style.color = '#b91c1c';
        statusDiv.innerText = 'Erro ao enviar: ' + e;
      }
    }

    async function dispararNtfy() {
      const statusDiv = document.getElementById('alexa-status');
      statusDiv.style.display = 'block';
      statusDiv.style.background = '#f0f9ff';
      statusDiv.style.color = '#0284c7';
      statusDiv.innerText = 'Disparando alerta no celular...';

      try {
        const res = await fetch('/api/testar-ntfy', { method: 'POST' });
        const d = await res.json();
        statusDiv.style.background = '#dcfce7';
        statusDiv.style.color = '#15803d';
        statusDiv.innerText = d.mensagem || 'Notificação enviada ao celular!';
      } catch (e) {
        statusDiv.style.background = '#fee2e2';
        statusDiv.style.color = '#b91c1c';
        statusDiv.innerText = 'Erro ao disparar: ' + e;
      }
    }

    async function salvarConfig(e) {
      e.preventDefault();
      const form = document.getElementById('form-config');
      const formData = new FormData(form);
      const params = new URLSearchParams(formData);

      try {
        await fetch('/api/config', { method: 'POST', body: params });
        alert('Configurações salvas no ESP32!');
      } catch (err) {
        alert('Erro ao salvar: ' + err);
      }
    }

    carregarDadosAoVivo();
    carregarHistorico();
    setInterval(carregarDadosAoVivo, 3000);
  </script>
</body>
</html>
"""

class DashboardHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/" or self.path.startswith("/?"):
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(HTML_PAGE.encode("utf-8"))
        elif self.path == "/api/dados":
            dados = obter_dados_tempo_real()
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(json.dumps(dados, ensure_ascii=False).encode("utf-8"))
        elif self.path == "/api/historico":
            historico = processar_historico_completo()
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(json.dumps(historico, ensure_ascii=False).encode("utf-8"))
        elif self.path.startswith("/api/historico-dia"):
            query = urllib.parse.urlparse(self.path).query
            params = urllib.parse.parse_qs(query)
            data_req = params.get("data", [None])[0]
            if data_req:
                historico = processar_historico_completo()
                dia_data = historico.get(data_req)
                if dia_data:
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json; charset=utf-8")
                    self.send_header("Access-Control-Allow-Origin", "*")
                    self.end_headers()
                    self.wfile.write(json.dumps(dia_data, ensure_ascii=False).encode("utf-8"))
                    return
            self.send_response(404)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.end_headers()
            self.wfile.write(json.dumps({"erro": "Data nao encontrada"}).encode("utf-8"))
        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self):
        content_len = int(self.headers.get('Content-Length', 0))
        post_body = self.rfile.read(content_len) if content_len > 0 else b""
        destino_url = f"{ESP32_BASE}{self.path}"
        try:
            req = urllib.request.Request(destino_url, data=post_body, method="POST")
            for h, v in self.headers.items():
                if h.lower() in ["content-type", "accept"]:
                    req.add_header(h, v)
            with urllib.request.urlopen(req, timeout=5) as resp:
                resp_data = resp.read()
                self.send_response(resp.status)
                self.send_header("Content-Type", resp.headers.get("Content-Type", "application/json"))
                self.end_headers()
                self.wfile.write(resp_data)
                return
        except Exception as e:
            self.send_response(200)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.end_headers()
            self.wfile.write(json.dumps({"status": "ok", "mensagem": f"Simulado localmente ({e})"}).encode("utf-8"))

def main():
    server = HTTPServer(("0.0.0.0", PORTA), DashboardHandler)
    print(f"=== Servidor Dashboard Ativo na porta {PORTA} ===")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        server.server_close()

if __name__ == "__main__":
    main()
