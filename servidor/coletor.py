#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Coletor de Dados Climáticos - ESP32 para Servidor SMB
Salva leituras em CSV, gera relatórios (diário, semanal, mensal) e calcula o
recap do "Dia com mais variação de clima", sincronizando com o painel do ESP32.
"""

import os
import sys
import time
import json
import csv
import logging
from datetime import datetime
from collections import defaultdict
import requests

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    handlers=[logging.StreamHandler(sys.stdout)]
)

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
CONFIG_FILE = os.path.join(SCRIPT_DIR, "config.json")

def carregar_config():
    config_padrao = {
        "esp32_url": "http://192.168.0.XXX/api/dados",
        "intervalo_segundos": 300,
        "pasta_dados": os.path.join(SCRIPT_DIR, "dados")
    }
    if os.path.exists(CONFIG_FILE):
        try:
            with open(CONFIG_FILE, "r", encoding="utf-8") as f:
                cfg = json.load(f)
                config_padrao.update(cfg)
        except Exception as e:
            logging.warning(f"Erro ao ler config.json, usando padrão: {e}")
    else:
        try:
            with open(CONFIG_FILE, "w", encoding="utf-8") as f:
                json.dump(config_padrao, f, indent=2, ensure_ascii=False)
        except Exception as e:
            logging.warning(f"Não foi possível salvar config padrão: {e}")
    return config_padrao

def garantir_diretorios(pasta_dados):
    os.makedirs(pasta_dados, exist_ok=True)

def registrar_leitura(pasta_dados, dados):
    agora = datetime.now()
    mes_str = agora.strftime("%Y-%m")
    arquivo_mes = os.path.join(pasta_dados, f"leituras_{mes_str}.csv")
    existe = os.path.exists(arquivo_mes)

    interno = dados.get("interno", {})
    externo = dados.get("externo") or dados.get("externo_sp", {})

    linha = [
        agora.strftime("%Y-%m-%d %H:%M:%S"),
        interno.get("temperatura", ""),
        interno.get("umidade", ""),
        externo.get("temperatura", ""),
        externo.get("umidade", ""),
        externo.get("temp_max", ""),
        externo.get("temp_min", ""),
        externo.get("probabilidade_chuva", ""),
        externo.get("condicao", "")
    ]

    with open(arquivo_mes, "a", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        if not existe:
            writer.writerow([
                "DataHora",
                "Temp_Interna_C",
                "Umid_Interna_Pct",
                "Temp_Externa_C",
                "Umid_Externa_Pct",
                "Temp_Max_Prevista_C",
                "Temp_Min_Prevista_C",
                "Probabilidade_Chuva_Pct",
                "Condicao_Tempo"
            ])
        writer.writerow(linha)

def processar_historico_dias(pasta_dados, max_dias=7):
    """Processa as leituras horárias dos últimos dias para renderização nativa de gráficos no ESP32."""
    todos_arquivos = sorted([
        os.path.join(pasta_dados, f)
        for f in os.listdir(pasta_dados)
        if f.startswith("leituras_") and f.endswith(".csv")
    ])
    dias = defaultdict(lambda: {
        "horas": {h: {"t_int": [], "u_int": [], "t_ext": []} for h in range(24)},
        "t_int_all": [],
        "u_int_all": [],
        "t_ext_all": [],
        "leituras": 0
    })

    for arq in todos_arquivos:
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
    datas_ordenadas = sorted(dias.keys(), reverse=True)
    if max_dias:
        datas_ordenadas = datas_ordenadas[:max_dias]

    for d in datas_ordenadas:
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

def obter_ip_local(ip_destino="8.8.8.8"):
    import socket
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect((ip_destino, 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"

def gerar_relatorios_e_recap(pasta_dados, url_esp32_base):
    """Calcula médias diárias, semanais, mensais e o recap do dia com mais variação de clima."""
    todos_arquivos = [
        os.path.join(pasta_dados, f) 
        for f in os.listdir(pasta_dados) 
        if f.startswith("leituras_") and f.endswith(".csv")
    ]
    if not todos_arquivos:
        return

    diario = {}   # chave: 'YYYY-MM-DD'
    semanal = {}  # chave: 'YYYY-Wxx'
    mensal = {}   # chave: 'YYYY-MM'

    for arq in todos_arquivos:
        try:
            with open(arq, "r", encoding="utf-8") as f:
                reader = csv.DictReader(f)
                for row in reader:
                    dh_str = row.get("DataHora")
                    if not dh_str:
                        continue
                    dt = datetime.strptime(dh_str, "%Y-%m-%d %H:%M:%S")

                    try:
                        t_int = float(row.get("Temp_Interna_C"))
                        u_int = float(row.get("Umid_Interna_Pct"))
                    except (ValueError, TypeError):
                        continue

                    try:
                        t_ext = float(row.get("Temp_Externa_C"))
                        u_ext = float(row.get("Umid_Externa_Pct"))
                    except (ValueError, TypeError):
                        t_ext = None
                        u_ext = None

                    def acumular(dicionario, chave):
                        if chave not in dicionario:
                            dicionario[chave] = {
                                "t_int": [], "u_int": [],
                                "t_ext": [], "u_ext": []
                            }
                        dicionario[chave]["t_int"].append(t_int)
                        dicionario[chave]["u_int"].append(u_int)
                        if t_ext is not None:
                            dicionario[chave]["t_ext"].append(t_ext)
                        if u_ext is not None:
                            dicionario[chave]["u_ext"].append(u_ext)

                    acumular(diario, dt.strftime("%Y-%m-%d"))
                    acumular(semanal, f"{dt.isocalendar()[0]}-W{dt.isocalendar()[1]:02d}")
                    acumular(mensal, dt.strftime("%Y-%m"))
        except Exception as e:
            logging.error(f"Erro ao processar {arq}: {e}")

    # Salva tabelas CSV consolidadas
    def salvar_tabela(nome_arquivo, cabecalho_chave, dicionario):
        caminho = os.path.join(pasta_dados, nome_arquivo)
        with open(caminho, "w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f)
            writer.writerow([
                cabecalho_chave,
                "Temp_Int_Min", "Temp_Int_Max", "Temp_Int_Media", "Umid_Int_Media",
                "Temp_Ext_Min", "Temp_Ext_Max", "Temp_Ext_Media", "Umid_Ext_Media",
                "Amostras"
            ])
            for k in sorted(dicionario.keys()):
                d = dicionario[k]
                t_int_min = min(d["t_int"]) if d["t_int"] else ""
                t_int_max = max(d["t_int"]) if d["t_int"] else ""
                t_int_avg = round(sum(d["t_int"]) / len(d["t_int"]), 1) if d["t_int"] else ""
                u_int_avg = round(sum(d["u_int"]) / len(d["u_int"]), 1) if d["u_int"] else ""

                t_ext_min = min(d["t_ext"]) if d["t_ext"] else ""
                t_ext_max = max(d["t_ext"]) if d["t_ext"] else ""
                t_ext_avg = round(sum(d["t_ext"]) / len(d["t_ext"]), 1) if d["t_ext"] else ""
                u_ext_avg = round(sum(d["u_ext"]) / len(d["u_ext"]), 1) if d["u_ext"] else ""

                writer.writerow([
                    k,
                    t_int_min, t_int_max, t_int_avg, u_int_avg,
                    t_ext_min, t_ext_max, t_ext_avg, u_ext_avg,
                    len(d["t_int"])
                ])

    salvar_tabela("relatorio_diario.csv", "Data", diario)
    salvar_tabela("relatorio_semanal.csv", "Semana", semanal)
    salvar_tabela("relatorio_mensal.csv", "Mes", mensal)

    # 1. Calcula Médias do período atual
    hoje_str = datetime.now().strftime("%Y-%m-%d")
    semana_str = f"{datetime.now().isocalendar()[0]}-W{datetime.now().isocalendar()[1]:02d}"
    mes_str = datetime.now().strftime("%Y-%m")

    def calc_media(dicionario, chave, campo):
        if chave in dicionario and dicionario[chave][campo]:
            lista = dicionario[chave][campo]
            return round(sum(lista) / len(lista), 1)
        return 0.0

    media_diaria = calc_media(diario, hoje_str, "t_int")
    media_semanal = calc_media(semanal, semana_str, "t_int")
    media_mensal = calc_media(mensal, mes_str, "t_int")

    # 2. Calcula o Recap do "Dia com mais variação de clima"
    dia_maior_var = "--"
    maior_delta_t = 0.0
    t_min_var = 0.0
    t_max_var = 0.0
    delta_u_var = 0.0
    u_min_var = 0.0
    u_max_var = 0.0

    for d_str, dados in diario.items():
        if len(dados["t_int"]) >= 2:
            t_min = min(dados["t_int"])
            t_max = max(dados["t_int"])
            delta_t = round(t_max - t_min, 1)

            u_min = min(dados["u_int"])
            u_max = max(dados["u_int"])
            delta_u = round(u_max - u_min, 1)

            if delta_t > maior_delta_t:
                maior_delta_t = delta_t
                dia_maior_var = d_str
                t_min_var = t_min
                t_max_var = t_max
                delta_u_var = delta_u
                u_min_var = u_min
                u_max_var = u_max

    # 3. Processa datas disponíveis e janela recente de 7 dias para o ESP32
    todos_dias = processar_historico_dias(pasta_dados, max_dias=None)
    datas_todas = sorted(list(todos_dias.keys()))
    dias_detalhado = {d: todos_dias[d] for d in datas_todas[-7:]} if len(datas_todas) > 7 else todos_dias

    resumo_painel = {
        "media_diaria": media_diaria,
        "media_semanal": media_semanal,
        "media_mensal": media_mensal,
        "recap_data": dia_maior_var,
        "recap_delta_temp": maior_delta_t,
        "recap_temp_min": t_min_var,
        "recap_temp_max": t_max_var,
        "recap_delta_umid": delta_u_var,
        "recap_umid_min": u_min_var,
        "recap_umid_max": u_max_var,
        "total_dias": len(diario),
        "datas_disponiveis": datas_todas,
        "servidor_api": f"http://{obter_ip_local()}:8089",
        "dias": dias_detalhado
    }

    # Salva JSON localmente no disco
    with open(os.path.join(pasta_dados, "resumo_recente.json"), "w", encoding="utf-8") as f:
        json.dump(resumo_painel, f, indent=2, ensure_ascii=False)

    # Envia resumo e histórico horário para o ESP32 atualizar o painel dinamicamente
    if url_esp32_base:
        url_historico = f"{url_esp32_base}/api/historico"
        try:
            payload_json = json.dumps(resumo_painel, separators=(',', ':'), ensure_ascii=False)
            res = requests.post(url_historico, data=payload_json, headers={'Content-Type': 'application/json'}, timeout=5)
            logging.info(f"Resumo e Histórico 24h sincronizados com sucesso no ESP32 ({len(payload_json)} bytes, status {res.status_code})!")
        except Exception as e:
            logging.debug(f"Não foi possível sincronizar histórico com o ESP32 ({e})")

def main():
    logging.info("=== Iniciando Coletor de Dados ESP32 ===")
    config = carregar_config()
    pasta_dados = config["pasta_dados"]
    garantir_diretorios(pasta_dados)

    url_esp = config["esp32_url"]
    # Extrai URL base (ex: http://192.168.0.x)
    url_base = url_esp.rsplit("/api/", 1)[0] if "/api/" in url_esp else url_esp

    intervalo = int(config.get("intervalo_segundos", 300))
    logging.info(f"Conectando ao ESP32 em: {url_esp}")
    logging.info(f"Intervalo de coleta: {intervalo} segundos")
    logging.info(f"Destino dos dados: {pasta_dados}")

    ciclos_relatorio = 0

    while True:
        try:
            resposta = requests.get(url_esp, timeout=10)
            if resposta.status_code == 200:
                dados = resposta.json()
                t_int = dados.get("interno", {}).get("temperatura")
                u_int = dados.get("interno", {}).get("umidade")
                externo = dados.get("externo") or dados.get("externo_sp", {})
                t_ext = externo.get("temperatura")
                u_ext = externo.get("umidade")
                logging.info(f"Leitura OK -> Interna: {t_int}°C, {u_int}% | Externa (SP): {t_ext}°C, {u_ext}%")

                registrar_leitura(pasta_dados, dados)

                ciclos_relatorio += 1
                # Atualiza relatórios e recap a cada coleta para alimentar o painel
                gerar_relatorios_e_recap(pasta_dados, url_base)
            else:
                logging.warning(f"ESP32 respondeu com status {resposta.status_code}")
        except requests.exceptions.RequestException as e:
            logging.warning(f"Falha ao conectar no ESP32 ({url_esp}): {e}")
        except Exception as e:
            logging.error(f"Erro inesperado: {e}")

        time.sleep(intervalo)

if __name__ == "__main__":
    main()
