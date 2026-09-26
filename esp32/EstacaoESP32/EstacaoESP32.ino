#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>

// =========================================================================
// CONFIGURACOES DE CONEXAO WI-FI
// =========================================================================
const char* ssid = "SUA_REDE_WIFI";
const char* password = "SUA_SENHA_WIFI";

// =========================================================================
// SENSOR DHT11 (GPIO 4)
// =========================================================================
#define DHTPIN 4
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);
Preferences prefs;

// =========================================================================
// DADOS EM TEMPO REAL
// =========================================================================
float tempInterna = 0.0;
float umidInterna = 0.0;
unsigned long ultimoTempoDHT = 0;

// Localizacao e CEP configuravel (Exemplo: Praca da Se, SP)
String cepConfig = "01001000";
String localizacaoNome = "Sao Paulo";
float latLocal = -23.5505;
float lonLocal = -46.6333;

// Previsao Externa (Open-Meteo)
float tempExterna = 0.0;
float umidExterna = 0.0;
float tempMaxExterna = 0.0;
float tempMinExterna = 0.0;
int chuvaProbabilidade = 0;
String condicaoTempo = "Carregando...";
unsigned long ultimoTempoPrevisao = 0;
// Atualizacao externa a cada 30 minutos (1800000 ms) para poupar recursos
const unsigned long INTERVALO_PREVISAO = 30 * 60 * 1000;

// Configuracoes de Avisos Diarios
int horaAviso = 8;
int minutoAviso = 0;
bool avisoAtivo = true;
String ntfyTopic = "seu-topico-ntfy";
String vmToken = "SEU_TOKEN_VOICE_MONKEY";
String vmDevice = "seu-dispositivo-alexa";
int ultimoDiaAvisoEnviado = -1;

// Falas personalizaveis configuradas pelo painel web
String falaBase = "Bom dia. {pausa} A temperatura atualmente e {temp_in} graus, {pausa_curta} la fora esta {temp_ext} graus com {condicao}.";
String falaChuva = "Nao esqueca o guarda-chuva.";
String falaCalor = "Vai fazer calor, beba bastante agua.";
String falaFrio = "Leve um agasalho, vai esfriar.";
String falaSeco = "O ar esta seco, hidrate-se bem.";
String falaBom = "Tenha um excelente dia.";

// Historico e Recap sincronizados com o Servidor SMB (HDD)
float mediaDiaria = 0.0;
float mediaSemanal = 0.0;
float mediaMensal = 0.0;
String recapData = "--";
float recapDeltaTemp = 0.0;
float recapTempMin = 0.0;
float recapTempMax = 0.0;
float recapDeltaUmid = 0.0;
float recapUmidMin = 0.0;
float recapUmidMax = 0.0;
int totalDiasAmostrados = 0;
String jsonHistoricoCompleto = "{}";
String jsonDatasDisponiveis = "[]";
String servidorHost = "http://IP_DO_SEU_SERVIDOR:8088";

// Acumulador local para medias caso o servidor esteja iniciando
float somaTempLocal = 0.0;
int contagemTempLocal = 0;

// =========================================================================
// MAPEAMENTO DE CONDICOES DE TEMPO (WMO)
// =========================================================================
String interpretarCodigoWMO(int code) {
  switch (code) {
    case 0: return "Ceu limpo";
    case 1: return "Predominantemente ensolarado";
    case 2: return "Parcialmente nublado";
    case 3: return "Nublado";
    case 45: case 48: return "Nevoeiro";
    case 51: case 53: case 55: return "Garoa leve";
    case 61: case 63: case 65: return "Chuva";
    case 80: case 81: case 82: return "Pancadas de chuva";
    case 95: case 96: case 99: return "Tempestade com trovoes";
    default: return "Nublado";
  }
}

// =========================================================================
// GEOCODIFICACAO DO CEP VIA BRASILAPI
// =========================================================================
void resolverCoordenadasCEP(String cep) {
  if (WiFi.status() != WL_CONNECTED) return;

  // Limpa caracteres especiais do CEP
  String cepLimpo = "";
  for (unsigned int i = 0; i < cep.length(); i++) {
    if (isDigit(cep[i])) cepLimpo += cep[i];
  }
  if (cepLimpo.length() != 8) return;

  Serial.printf("[Geocode] Consultando coordenadas para o CEP: %s...\n", cepLimpo.c_str());
  HTTPClient http;
  String url = "http://brasilapi.com.br/api/cep/v2/" + cepLimpo;
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
      String cidade = doc["city"] | "Sao Paulo";
      String bairro = doc["neighborhood"] | "";
      String uf = doc["state"] | "SP";
      localizacaoNome = cidade + (bairro.length() > 0 ? " / " + bairro : "") + " (" + uf + ")";

      const char* latStr = doc["location"]["coordinates"]["latitude"];
      const char* lonStr = doc["location"]["coordinates"]["longitude"];
      if (latStr && lonStr) {
        latLocal = atof(latStr);
        lonLocal = atof(lonStr);
        Serial.printf("[Geocode] Local: %s | Lat: %.4f | Lon: %.4f\n", localizacaoNome.c_str(), latLocal, lonLocal);
      }
    }
  }
  http.end();
}

// =========================================================================
// CONSULTA PREVISAO EXTERNA (OPEN-METEO)
// =========================================================================
void atualizarPrevisaoExterna() {
  if (WiFi.status() != WL_CONNECTED) return;

  Serial.println("[Clima] Atualizando previsao externa...");
  HTTPClient http;
  String url = "http://api.open-meteo.com/v1/forecast?latitude=" + String(latLocal, 4) +
               "&longitude=" + String(lonLocal, 4) +
               "&current=temperature_2m,relative_humidity_2m,weather_code" +
               "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max" +
               "&timezone=America%2FSao_Paulo";

  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      tempExterna = doc["current"]["temperature_2m"] | tempExterna;
      umidExterna = doc["current"]["relative_humidity_2m"] | umidExterna;
      int code = doc["current"]["weather_code"] | 0;
      condicaoTempo = interpretarCodigoWMO(code);

      tempMaxExterna = doc["daily"]["temperature_2m_max"][0] | tempMaxExterna;
      tempMinExterna = doc["daily"]["temperature_2m_min"][0] | tempMinExterna;
      chuvaProbabilidade = doc["daily"]["precipitation_probability_max"][0] | chuvaProbabilidade;

      Serial.printf("[Clima] Externo: %.1f C | Umid: %.1f %% | Max: %.1f C | Min: %.1f C | Chuva: %d %%\n",
                    tempExterna, umidExterna, tempMaxExterna, tempMinExterna, chuvaProbabilidade);
    }
  }
  http.end();
}

// =========================================================================
// OBTENCAO DE DATA E HORA VIA NTP
// =========================================================================
String obterHoraFormatada() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "--:--:--";
  char buf[16];
  strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
  return String(buf);
}

String obterDataFormatada() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "----/--/--";
  char buf[16];
  strftime(buf, sizeof(buf), "%d/%m/%Y", &timeinfo);
  return String(buf);
}

// =========================================================================
// GERADOR DE AVISOS INTELIGENTES (CONFIGURAVEIS, RESUMIDOS E COM PAUSAS)
// =========================================================================
String aplicarTagsFala(String templateTexto, bool ehParaAlexa = true) {
  templateTexto.replace("{temp_in}", String((int)round(tempInterna)));
  templateTexto.replace("{temp_ext}", String((int)round(tempExterna)));
  templateTexto.replace("{umid_in}", String((int)round(umidInterna)));
  templateTexto.replace("{umid_ext}", String((int)round(umidExterna)));
  templateTexto.replace("{max}", String((int)round(tempMaxExterna)));
  templateTexto.replace("{min}", String((int)round(tempMinExterna)));
  templateTexto.replace("{chuva}", String(chuvaProbabilidade));
  templateTexto.replace("{condicao}", condicaoTempo);
  templateTexto.replace("{local}", localizacaoNome);

  if (ehParaAlexa) {
    // Insere pausas SSML reais compativeis com a Alexa
    templateTexto.replace("{pausa}", "<break time=\"800ms\"/>");
    templateTexto.replace("{pausa_curta}", "<break time=\"400ms\"/>");
    templateTexto.replace("{pausa_longa}", "<break time=\"1.5s\"/>");
  } else {
    // No celular (NTFY), converte pausas em pontuacao limpa
    templateTexto.replace("{pausa}", "... ");
    templateTexto.replace("{pausa_curta}", ", ");
    templateTexto.replace("{pausa_longa}", "\n");
    // Remove qualquer tag SSML manual para nao poluir o celular
    int pos = 0;
    while ((pos = templateTexto.indexOf("<break")) >= 0) {
      int endPos = templateTexto.indexOf("/>", pos);
      if (endPos > pos) {
        templateTexto.remove(pos, (endPos - pos) + 2);
      } else {
        break;
      }
    }
  }
  return templateTexto;
}

String gerarTextoAvisoClima(String textoPersonalizado = "", bool ehParaAlexa = true) {
  if (textoPersonalizado.length() > 0) {
    return aplicarTagsFala(textoPersonalizado, ehParaAlexa);
  }

  // 1. Frase Base Resumida
  String msg = aplicarTagsFala(falaBase, ehParaAlexa);

  // 2. Conselho ou aviso especifico por tipo de clima
  String conselho = "";
  if (chuvaProbabilidade >= 40 || condicaoTempo.indexOf("Chuva") >= 0 || condicaoTempo.indexOf("Garoa") >= 0) {
    conselho = aplicarTagsFala(falaChuva, ehParaAlexa);
  } else if (tempMaxExterna >= 28.0) {
    conselho = aplicarTagsFala(falaCalor, ehParaAlexa);
  } else if (tempMinExterna <= 16.0) {
    conselho = aplicarTagsFala(falaFrio, ehParaAlexa);
  } else if (umidExterna <= 35.0 || umidInterna <= 35.0) {
    conselho = aplicarTagsFala(falaSeco, ehParaAlexa);
  } else {
    conselho = aplicarTagsFala(falaBom, ehParaAlexa);
  }

  if (conselho.length() > 0) {
    if (ehParaAlexa) {
      msg += " <break time=\"700ms\"/> " + conselho;
    } else {
      msg += "\n" + conselho;
    }
  }

  return msg;
}

// =========================================================================
// ENVIO DE AVISOS (VOICE MONKEY ALEXA + NTFY CELULAR)
// =========================================================================
bool enviarAvisos(String textoMensagem = "", bool enviarAlexa = true, bool enviarNtfy = true) {
  if (WiFi.status() != WL_CONNECTED) return false;

  String textoAlexa = gerarTextoAvisoClima(textoMensagem, true);
  String textoNtfy = gerarTextoAvisoClima(textoMensagem, false);

  Serial.println("\n--- Enviando Notificacoes ---");
  Serial.printf("[Texto Alexa]: %s\n", textoAlexa.c_str());

  // 1. Envio para o NTFY com prioridade URGENT (Nivel 5 para forcar toque/tela)
  if (enviarNtfy && ntfyTopic.length() > 0) {
    Serial.printf("[NTFY] Enviando para http://ntfy.sh/%s com prioridade urgente...\n", ntfyTopic.c_str());
    HTTPClient httpNtfy;
    String ntfyUrl = "http://ntfy.sh/" + ntfyTopic;
    httpNtfy.begin(ntfyUrl);
    httpNtfy.addHeader("Title", "Estacao Clima - " + localizacaoNome);
    httpNtfy.addHeader("Priority", "urgent"); // Nivel 5: acorda celular e soa alarme
    httpNtfy.addHeader("Tags", "thermometer,cloud");

    int resNtfy = httpNtfy.POST(textoNtfy);
    Serial.printf("[NTFY] Resposta do servidor: HTTP %d\n", resNtfy);
    httpNtfy.end();
  }

  // 2. Envio para a ALEXA via Voice Monkey v3 (com pausas SSML)
  if (enviarAlexa && vmToken.length() > 0 && vmDevice.length() > 0) {
    Serial.println("[Alexa] Enviando para a Alexa via Voice Monkey API v3...");
    WiFiClientSecure clientSecure;
    clientSecure.setInsecure();

    HTTPClient httpVM;
    if (httpVM.begin(clientSecure, "https://api-v3.voicemonkey.io/announce")) {
      httpVM.addHeader("Content-Type", "application/json");

      JsonDocument vmDoc;
      vmDoc["token"] = vmToken;
      vmDoc["device"] = vmDevice;
      vmDoc["speech"] = textoAlexa;
      vmDoc["language"] = "pt-BR";

      String vmPayload;
      serializeJson(vmDoc, vmPayload);

      int resVM = httpVM.POST(vmPayload);
      Serial.printf("[Alexa] Resposta Voice Monkey: HTTP %d\n", resVM);
      httpVM.end();
    }
  }

  return true;
}

// =========================================================================
// ROTAS DA API
// =========================================================================

// Endpoint de dados completos para o coletor PM2 e atualizacoes web
void handleApiDados() {
  JsonDocument doc;
  doc["timestamp"] = obterDataFormatada() + " " + obterHoraFormatada();
  doc["hora_atual"] = obterHoraFormatada();
  doc["data_atual"] = obterDataFormatada();

  JsonObject interno = doc["interno"].to<JsonObject>();
  interno["temperatura"] = tempInterna;
  interno["umidade"] = umidInterna;

  JsonObject externo = doc["externo"].to<JsonObject>();
  externo["cep"] = cepConfig;
  externo["local"] = localizacaoNome;
  externo["temperatura"] = tempExterna;
  externo["umidade"] = umidExterna;
  externo["temp_max"] = tempMaxExterna;
  externo["temp_min"] = tempMinExterna;
  externo["probabilidade_chuva"] = chuvaProbabilidade;
  externo["condicao"] = condicaoTempo;

  JsonObject historico = doc["historico"].to<JsonObject>();
  historico["media_diaria"] = (mediaDiaria > 0.0) ? mediaDiaria : (contagemTempLocal > 0 ? round((somaTempLocal / contagemTempLocal) * 10) / 10 : tempInterna);
  historico["media_semanal"] = mediaSemanal;
  historico["media_mensal"] = mediaMensal;

  JsonObject recap = historico["recap"].to<JsonObject>();
  recap["data"] = recapData;
  recap["delta_temp"] = recapDeltaTemp;
  recap["temp_min"] = recapTempMin;
  recap["temp_max"] = recapTempMax;
  recap["delta_umid"] = recapDeltaUmid;
  recap["umid_min"] = recapUmidMin;
  recap["umid_max"] = recapUmidMax;

  JsonObject sistema = doc["sistema"].to<JsonObject>();
  sistema["temp_chip"] = round(temperatureRead());
  sistema["ram_livre_kb"] = ESP.getFreeHeap() / 1024;
  sistema["sinal_wifi_dbm"] = WiFi.RSSI();
  sistema["uptime_segundos"] = millis() / 1000;

  String resposta;
  serializeJson(doc, resposta);
  server.send(200, "application/json", resposta);
}

// Recebe ou serve o historico detalhado com resolucao horaria
void handleApiHistorico() {
  if (server.method() == HTTP_POST) {
    if (server.hasArg("plain")) {
      String payload = server.arg("plain");
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, payload);
      if (!err) {
        mediaDiaria = doc["media_diaria"] | mediaDiaria;
        mediaSemanal = doc["media_semanal"] | mediaSemanal;
        mediaMensal = doc["media_mensal"] | mediaMensal;

        const char* dStr = doc["recap_data"];
        if (dStr) recapData = String(dStr);
        recapDeltaTemp = doc["recap_delta_temp"] | recapDeltaTemp;
        recapTempMin = doc["recap_temp_min"] | recapTempMin;
        recapTempMax = doc["recap_temp_max"] | recapTempMax;
        recapDeltaUmid = doc["recap_delta_umid"] | recapDeltaUmid;
        recapUmidMin = doc["recap_umid_min"] | recapUmidMin;
        recapUmidMax = doc["recap_umid_max"] | recapUmidMax;
        totalDiasAmostrados = doc["total_dias"] | totalDiasAmostrados;

        if (doc.containsKey("servidor_api")) {
          const char* sHost = doc["servidor_api"];
          if (sHost) servidorHost = String(sHost);
        }

        if (doc.containsKey("datas_disponiveis")) {
          String datasStr;
          serializeJson(doc["datas_disponiveis"], datasStr);
          jsonDatasDisponiveis = datasStr;
        }

        if (doc.containsKey("dias")) {
          String diasJson;
          serializeJson(doc["dias"], diasJson);
          jsonHistoricoCompleto = diasJson;
        } else {
          jsonHistoricoCompleto = payload;
        }

        Serial.printf("[Historico] Sincronizado e armazenado na RAM (%d bytes)!\n", jsonHistoricoCompleto.length());
        server.send(200, "application/json", "{\"status\":\"ok\"}");
        return;
      }
    }
  }

  // Se for requisicao GET /api/historico, serve as datas disponiveis e o cache
  String resp = "{\"datas\":" + jsonDatasDisponiveis + ",\"dias\":" + jsonHistoricoCompleto + "}";
  server.send(200, "application/json", resp);
}

// Consulta sob demanda um dia especifico do mes no servidor SMB
void handleApiHistoricoDia() {
  if (!server.hasArg("data")) {
    server.send(400, "application/json", "{\"erro\":\"parametro data faltando\"}");
    return;
  }
  String dataReq = server.arg("data");

  if (WiFi.status() == WL_CONNECTED && servidorHost.length() > 5) {
    HTTPClient http;
    String url = servidorHost + "/api/historico-dia?data=" + dataReq;
    http.begin(url);
    http.setTimeout(4000);
    int code = http.GET();
    if (code == 200) {
      String payload = http.getString();
      server.send(200, "application/json", payload);
      http.end();
      return;
    }
    http.end();
  }
  server.send(404, "application/json", "{\"erro\":\"data nao disponivel\"}");
}

// Salva configuracoes do formulario web
void handleSalvarConfig() {
  if (server.hasArg("cep")) {
    String novoCep = server.arg("cep");
    novoCep.trim();
    if (novoCep.length() >= 8 && novoCep != cepConfig) {
      cepConfig = novoCep;
      prefs.putString("cep", cepConfig);
      resolverCoordenadasCEP(cepConfig);
      atualizarPrevisaoExterna();
    }
  }

  if (server.hasArg("hora")) horaAviso = server.arg("hora").toInt();
  if (server.hasArg("minuto")) minutoAviso = server.arg("minuto").toInt();
  avisoAtivo = server.hasArg("ativo") && (server.arg("ativo") == "1" || server.arg("ativo") == "true");

  if (server.hasArg("ntfy")) ntfyTopic = server.arg("ntfy");
  if (server.hasArg("vm_token")) vmToken = server.arg("vm_token");
  if (server.hasArg("vm_device")) vmDevice = server.arg("vm_device");

  if (server.hasArg("fala_base")) falaBase = server.arg("fala_base");
  if (server.hasArg("fala_chuva")) falaChuva = server.arg("fala_chuva");
  if (server.hasArg("fala_calor")) falaCalor = server.arg("fala_calor");
  if (server.hasArg("fala_frio")) falaFrio = server.arg("fala_frio");
  if (server.hasArg("fala_seco")) falaSeco = server.arg("fala_seco");
  if (server.hasArg("fala_bom")) falaBom = server.arg("fala_bom");

  prefs.putInt("hora", horaAviso);
  prefs.putInt("minuto", minutoAviso);
  prefs.putBool("ativo", avisoAtivo);
  prefs.putString("ntfy", ntfyTopic);
  prefs.putString("vm_token", vmToken);
  prefs.putString("vm_device", vmDevice);
  prefs.putString("local", localizacaoNome);
  prefs.putFloat("lat", latLocal);
  prefs.putFloat("lon", lonLocal);

  prefs.putString("f_base", falaBase);
  prefs.putString("f_chuva", falaChuva);
  prefs.putString("f_calor", falaCalor);
  prefs.putString("f_frio", falaFrio);
  prefs.putString("f_seco", falaSeco);
  prefs.putString("f_bom", falaBom);

  Serial.println("[Config] Novas configuracoes salvas com sucesso!");
  server.sendHeader("Location", "/");
  server.send(303);
}

// Disparo de teste para Alexa com mensagem personalizada
void handleFalarAlexa() {
  String texto = "";
  if (server.hasArg("plain")) {
    JsonDocument doc;
    deserializeJson(doc, server.arg("plain"));
    const char* t = doc["texto"];
    if (t) texto = String(t);
  }
  enviarAvisos(texto, true, false); // Apenas Alexa
  server.send(200, "application/json", "{\"status\":\"ok\",\"mensagem\":\"Mensagem enviada para a Alexa!\"}");
}

// Disparo de teste para o celular (NTFY)
void handleTestarNtfy() {
  enviarAvisos("", false, true); // Apenas NTFY
  server.send(200, "application/json", "{\"status\":\"ok\",\"mensagem\":\"Notificacao enviada ao celular!\"}");
}

// =========================================================================
// PAGINA WEB PRINCIPAL
// =========================================================================
void handleRoot() {
  String html = R"rawliteral(<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Estacao Meteorologica</title>
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
      --card-radius: 14px;
      --shadow-sm: 0 1px 3px rgba(0,0,0,0.03), 0 1px 2px rgba(0,0,0,0.02);
      --shadow-md: 0 4px 6px -1px rgba(0,0,0,0.04), 0 2px 4px -2px rgba(0,0,0,0.03);
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; }
    body { background-color: var(--bg); color: var(--text-main); padding: 2rem 1.5rem; display: flex; justify-content: center; min-height: 100vh; }
    .container { max-width: 960px; width: 100%; display: flex; flex-direction: column; gap: 1.5rem; }

    /* Cabecalho */
    header { display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 1rem; }
    .header-left { display: flex; flex-direction: column; gap: 0.15rem; }
    .header-title { font-size: 1.35rem; font-weight: 800; letter-spacing: -0.03em; color: var(--text-main); }

    .header-right { display: flex; align-items: center; gap: 0.75rem; }
    .live-dot-wrap { display: flex; align-items: center; gap: 0.4rem; font-size: 0.75rem; font-weight: 700; color: #16a34a; background: #f0fdf4; border: 1px solid #bbf7d0; padding: 0.35rem 0.7rem; border-radius: 999px; }
    .live-dot { width: 7px; height: 7px; background: #22c55e; border-radius: 50%; box-shadow: 0 0 0 2px rgba(34,197,94,0.25); }
    .clock-display { font-size: 0.95rem; font-weight: 700; color: var(--text-main); font-variant-numeric: tabular-nums; background: #ffffff; border: 1px solid var(--border); padding: 0.35rem 0.8rem; border-radius: 999px; box-shadow: var(--shadow-sm); }

    /* Abas Pilula */
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

    /* Metricas */
    .metric-main { display: flex; align-items: baseline; gap: 0.15rem; }
    .temp-val { font-size: 3.5rem; font-weight: 800; color: var(--text-main); line-height: 1; letter-spacing: -0.03em; }
    .unit { font-size: 1.75rem; font-weight: 600; color: var(--text-muted); margin-left: 0.15rem; }
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

    .grid-4 { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 0.75rem; }

    /* Barra de Selecao por Calendario */
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

    /* Grafico com Comparativo Interno x Externo */
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

    /* Formularios */
    .form-row { display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 1rem; }
    .form-group { display: flex; flex-direction: column; gap: 0.35rem; margin-top: 0.5rem; }
    label { font-size: 0.75rem; font-weight: 700; color: var(--text-muted); text-transform: uppercase; letter-spacing: 0.04em; }
    input[type="text"], input[type="number"] { width: 100%; border: 1px solid var(--border); border-radius: 8px; padding: 0.65rem 0.85rem; font-size: 0.95rem; color: var(--text-main); background: #ffffff; }
    input:focus { outline: none; border-color: var(--blue-primary); }
    .time-flex { display: flex; align-items: center; gap: 0.5rem; }
    .time-flex input { width: 75px; text-align: center; }

    button { cursor: pointer; border: 1px solid transparent; border-radius: 8px; padding: 0.65rem 1.25rem; font-size: 0.85rem; font-weight: 700; text-transform: uppercase; letter-spacing: 0.04em; transition: opacity 0.15s; }
    button:hover { opacity: 0.9; }
    .btn-primary { background: var(--blue-primary); color: #ffffff; }
    .btn-secondary { background: var(--blue-light); color: var(--blue-primary); border-color: var(--blue-border); }
    .help-text { font-size: 0.75rem; color: var(--text-muted); margin-top: 0.2rem; }
  </style>
</head>
<body>
  <div class="container">
    <!-- Cabecalho -->
    <header>
      <div class="header-left">
        <div class="header-title">Estacao Meteorologica</div>
      </div>
      <div class="header-right">
        <div class="live-dot-wrap">
          <div class="live-dot"></div>
          <span id="status-text">AO VIVO</span>
        </div>
        <div class="clock-display" id="relogio-visor">--:--:--</div>
      </div>
    </header>

    <!-- Navegacao em Abas -->
    <div class="tabs-nav">
      <button class="tab-btn active" onclick="trocarAba('tab-principal')">Painel Principal</button>
      <button class="tab-btn" onclick="trocarAba('tab-historico')">Historico 24h</button>
      <button class="tab-btn" onclick="trocarAba('tab-alexa')">Alexa & Notificacoes</button>
      <button class="tab-btn" onclick="trocarAba('tab-config')">Configuracoes</button>
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
            <div class="unit">C</div>
          </div>
          <div class="hum-val" id="umid-int">Umidade: -- %</div>
          <div class="info-footer">
            <span style="color:var(--text-muted); font-weight:600;">Sensacao Termica</span>
            <span class="info-pill" id="conforto">--</span>
          </div>
        </div>

        <!-- Sensor Externo -->
        <div class="card">
          <div class="card-label"><span>Ambiente Externo</span></div>
          <div class="metric-main">
            <div class="temp-val" id="temp-ext" style="color:var(--orange-primary);">--</div>
            <div class="unit">C</div>
          </div>
          <div class="hum-val" id="umid-ext" style="color:var(--orange-primary);">Umidade: -- %</div>
          <div class="info-footer">
            <span style="color:var(--text-muted); font-weight:600;" id="max-min">-- / --</span>
            <span class="info-pill" id="prob-chuva" style="background:var(--orange-light); color:var(--orange-primary);">Chuva 0%</span>
          </div>
        </div>
      </div>

      <!-- Banner Comparativo -->
      <div class="diff-banner" id="diff-banner">
        Calculando comparativo entre os ambientes...
      </div>

      <!-- Medias Consolidadas -->
      <div class="grid-3">
        <div class="stat-card">
          <div class="label">Media Hoje</div>
          <div class="val" id="med-hoje">-- C</div>
        </div>
        <div class="stat-card">
          <div class="label">Media Semanal</div>
          <div class="val" id="med-sem">-- C</div>
        </div>
        <div class="stat-card">
          <div class="label">Media Mensal</div>
          <div class="val" id="med-mes">-- C</div>
        </div>
      </div>

      <!-- Recap de Maior Variacao -->
      <div class="recap-box">
        <div class="recap-title">Recap: Maior Oscilacao Termica</div>
        <div class="recap-content" id="recap-texto">
          Carregando dados historicos...
        </div>
      </div>
    </div>

    <!-- ================================================================= -->
    <!-- ABA 2: HISTORICO 24H COM CALENDARIO E GRAFICO DUPLO -->
    <!-- ================================================================= -->
    <div id="tab-historico" class="tab-pane">
      <!-- Seletor de Data por Calendario -->
      <div class="calendar-bar">
        <div class="calendar-control-group">
          <button type="button" class="btn-nav-date" onclick="navegarData(-1)" title="Dia Anterior">&larr;</button>
          <div class="date-display-box" onclick="abrirCalendario()" title="Clique para abrir o calendario">
            <span style="font-size:1.05rem; line-height:1;">📅</span>
            <span id="data-formatada-br">--/--/----</span>
            <input type="date" id="data-calendario" class="hidden-date-input" onchange="selecionarDataCalendario(this.value)">
          </div>
          <button type="button" class="btn-nav-date" onclick="navegarData(1)" title="Proximo Dia">&rarr;</button>
        </div>
        <div style="display:flex; align-items:center; gap:0.6rem;">
          <button type="button" class="btn-today" onclick="selecionarHoje()">Hoje</button>
          <div id="data-info-badge" class="date-badge">Carregando...</div>
        </div>
      </div>

      <!-- Resumo do Dia Selecionado -->
      <div class="grid-4">
        <div class="stat-card" style="padding:1rem;">
          <div class="label">Media Interna</div>
          <div class="val" id="d-avg" style="font-size:1.6rem; color:var(--blue-primary);">-- C</div>
        </div>
        <div class="stat-card" style="padding:1rem;">
          <div class="label">Media Externa</div>
          <div class="val" id="d-ext-avg" style="font-size:1.6rem; color:var(--orange-primary);">-- C</div>
        </div>
        <div class="stat-card" style="padding:1rem;">
          <div class="label">Min / Max Interna</div>
          <div class="val" id="d-min-max" style="font-size:1.4rem; color:var(--text-main);">-- C / -- C</div>
        </div>
        <div class="stat-card" style="padding:1rem;">
          <div class="label">Variacao Interna</div>
          <div class="val" id="d-delta" style="font-size:1.6rem; color:#7c3aed;">-- C</div>
        </div>
        <div class="stat-card" style="padding:1rem;">
          <div class="label">Umidade Media</div>
          <div class="val" id="d-uavg" style="font-size:1.6rem; color:#0891b2;">-- %</div>
        </div>
      </div>

      <!-- Grafico SVG com Comparativo Interna x Externa -->
      <div class="chart-box">
        <div class="chart-top-bar">
          <div class="chart-title" id="chart-title">Comparativo Termico 24 Horas</div>
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
          <span>Detalhamento Hora a Hora (00:00 as 23:00)</span>
        </div>
        <div class="hours-grid" id="hours-grid"></div>
      </div>
    </div>

    <!-- ================================================================= -->
    <!-- ABA 3: ALEXA & NOTIFICACOES -->
    <!-- ================================================================= -->
    <div id="tab-alexa" class="tab-pane">
      <div class="card">
        <div class="card-label"><span>Comando de Voz para Alexa (Echo Dot)</span></div>
        <div style="display:flex; flex-direction:column; gap:0.85rem;">
          <div class="form-group" style="margin:0;">
            <label>Frase Personalizada:</label>
            <input type="text" id="msg-alexa" placeholder="Digite uma frase (ou deixe em branco para o resumo do dia)...">
          </div>
          <div style="display:flex; gap:0.75rem; flex-wrap:wrap;">
            <button type="button" class="btn-primary" onclick="falarAlexa()">Falar na Alexa Agora</button>
            <button type="button" class="btn-secondary" onclick="testarNtfy()">Testar Alerta no Celular (NTFY)</button>
          </div>
        </div>
      </div>
    </div>

    <!-- ================================================================= -->
    <!-- ABA 4: CONFIGURACOES -->
    <!-- ================================================================= -->
    <div id="tab-config" class="tab-pane">
      <!-- Telemetria e Monitoramento do ESP32 -->
      <div class="card">
        <div class="card-label"><span>Diagnostico & Telemetria do ESP32</span></div>
        <div class="grid-4">
          <div class="stat-card" style="padding:1rem;">
            <div class="label">Temperatura do Chip</div>
            <div class="val" id="sys-temp-chip" style="font-size:1.6rem; color:#dc2626;">-- C</div>
          </div>
          <div class="stat-card" style="padding:1rem;">
            <div class="label">Memoria RAM Livre</div>
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
        <div class="card-label"><span>Configuracoes do Dispositivo</span></div>
        <form action="/api/config" method="POST">
          <div class="form-row">
            <div class="form-group">
              <label>CEP:</label>
              <input type="text" name="cep" value=")rawliteral" + cepConfig + R"rawliteral(" placeholder="ex: 01136000" maxlength="9">
            </div>
            <div class="form-group">
              <label>Horario do Aviso Diario:</label>
              <div class="time-flex">
                <input type="number" name="hora" min="0" max="23" value=")rawliteral" + String(horaAviso) + R"rawliteral(" required>
                <span style="font-weight:700;">:</span>
                <input type="number" name="minuto" min="0" max="59" value=")rawliteral" + String(minutoAviso) + R"rawliteral(" required>
              </div>
            </div>
          </div>

          <div style="display:flex; align-items:center; gap:0.5rem; margin-top:1rem;">
            <input type="checkbox" id="ativo" name="ativo" value="1")rawliteral";
  if (avisoAtivo) html += " checked";
  html += R"rawliteral( style="width:16px; height:16px;">
            <label for="ativo" style="cursor:pointer; margin:0;">Ativar envio automatico de avisos diarios</label>
          </div>

          <div class="form-row" style="margin-top:0.75rem;">
            <div class="form-group">
              <label>Topico NTFY (Celular):</label>
              <input type="text" name="ntfy" id="cfg-ntfy" value=")rawliteral" + ntfyTopic + R"rawliteral(">
            </div>
            <div class="form-group">
              <label>Dispositivo Alexa (Speaker ID):</label>
              <input type="text" name="vm_device" value=")rawliteral" + vmDevice + R"rawliteral(">
            </div>
          </div>

          <div class="form-group" style="margin-top:0.75rem;">
            <label>Token Voice Monkey (v3):</label>
            <input type="text" name="vm_token" value=")rawliteral" + vmToken + R"rawliteral(">
          </div>

          <!-- Personalizacao de Falas -->
          <div style="margin-top:1.25rem; padding-top:1rem; border-top:1px solid var(--border);">
            <div style="font-size:0.8rem; font-weight:800; color:var(--blue-primary); text-transform:uppercase; margin-bottom:0.75rem;">
              Personalizacao das Falas da Alexa
            </div>
            <div class="form-group">
              <label>Frase Base Diaria:</label>
              <input type="text" name="fala_base" value=")rawliteral" + falaBase + R"rawliteral(">
            </div>
            <div class="form-row" style="margin-top:0.5rem;">
              <div class="form-group">
                <label>Aviso Chuva:</label>
                <input type="text" name="fala_chuva" value=")rawliteral" + falaChuva + R"rawliteral(">
              </div>
              <div class="form-group">
                <label>Aviso Calor:</label>
                <input type="text" name="fala_calor" value=")rawliteral" + falaCalor + R"rawliteral(">
              </div>
            </div>
            <div class="form-row" style="margin-top:0.5rem;">
              <div class="form-group">
                <label>Aviso Frio:</label>
                <input type="text" name="fala_frio" value=")rawliteral" + falaFrio + R"rawliteral(">
              </div>
              <div class="form-group">
                <label>Aviso Ar Seco:</label>
                <input type="text" name="fala_seco" value=")rawliteral" + falaSeco + R"rawliteral(">
              </div>
            </div>
            <div class="form-group" style="margin-top:0.5rem;">
              <label>Aviso Clima Ameno:</label>
              <input type="text" name="fala_bom" value=")rawliteral" + falaBom + R"rawliteral(">
            </div>
          </div>

          <div style="margin-top:1.25rem;">
            <button type="submit" class="btn-primary">Salvar Configuracoes</button>
          </div>
        </form>
      </div>
    </div>
  </div>

  <script>
    function trocarAba(abaId) {
      document.querySelectorAll('.tab-btn').forEach(function(b) { b.classList.remove('active'); });
      document.querySelectorAll('.tab-pane').forEach(function(p) { p.classList.remove('active'); });
      var btns = document.querySelectorAll('.tab-btn');
      for (var i = 0; i < btns.length; i++) {
        if (btns[i].getAttribute('onclick').indexOf(abaId) !== -1) {
          btns[i].classList.add('active');
          break;
        }
      }
      var pane = document.getElementById(abaId);
      if (pane) pane.classList.add('active');
      if (abaId === 'tab-historico' && datasDisponiveis.length === 0) {
        carregarHistorico();
      }
    }

    var historicoCompleto = {};
    var datasDisponiveis = [];
    var diaSelecionado = null;

    function abrirCalendario() {
      var el = document.getElementById('data-calendario');
      if (el.showPicker) {
        el.showPicker();
      } else {
        el.focus();
      }
    }

    async function carregarHistorico() {
      try {
        var res = await fetch('/api/historico');
        if (!res.ok) return;
        var data = await res.json();
        historicoCompleto = data.dias || {};
        datasDisponiveis = data.datas || Object.keys(historicoCompleto).sort();

        if (datasDisponiveis.length > 0) {
          var calInput = document.getElementById('data-calendario');
          calInput.min = datasDisponiveis[0];
          calInput.max = datasDisponiveis[datasDisponiveis.length - 1];

          var dataMaisRecente = datasDisponiveis[datasDisponiveis.length - 1];
          selecionarDataCalendario(dataMaisRecente);
        } else {
          document.getElementById('data-info-badge').innerText = 'Aguardando sincronizacao...';
        }
      } catch (e) {
        console.error("Erro historico:", e);
      }
    }

    async function selecionarDataCalendario(dataStr) {
      if (!dataStr) return;
      diaSelecionado = dataStr;
      document.getElementById('data-calendario').value = dataStr;
      var partes = dataStr.split('-');
      var dataBR = (partes.length === 3) ? (partes[2] + '/' + partes[1] + '/' + partes[0]) : dataStr;
      document.getElementById('data-formatada-br').innerText = dataBR;

      var badge = document.getElementById('data-info-badge');

      // Se o dia nao estiver no cache de 7 dias do ESP32, busca sob demanda no disco!
      if (!historicoCompleto[dataStr]) {
        badge.innerText = 'Buscando do disco...';
        badge.style.color = 'var(--blue-primary)';
        try {
          var resDia = await fetch('/api/historico-dia?data=' + dataStr);
          if (resDia.ok) {
            var dadosDia = await resDia.json();
            historicoCompleto[dataStr] = dadosDia;
          }
        } catch (err) {
          console.warn("Erro ao buscar historico do dia:", err);
        }
      }

      var d = historicoCompleto[dataStr];

      if (!d) {
        badge.innerText = 'Sem registros nesta data';
        badge.style.color = '#ef4444';
        document.getElementById('d-avg').innerText = '-- C';
        document.getElementById('d-ext-avg').innerText = '-- C';
        document.getElementById('d-min-max').innerText = '-- C / -- C';
        document.getElementById('d-delta').innerText = '-- C';
        document.getElementById('d-uavg').innerText = '-- %';
        document.getElementById('hours-grid').innerHTML = '<div style="grid-column:1/-1; padding:2rem; text-align:center; color:#94a3b8;">Nenhuma leitura foi gravada para este dia.</div>';
        document.getElementById('svg-chart').innerHTML = '<text x="50%" y="50%" text-anchor="middle" fill="#94a3b8" font-size="12">Sem dados registrados.</text>';
        return;
      }

      badge.innerText = d.leituras_total + ' leituras salvas';
      badge.style.color = 'var(--text-muted)';

      document.getElementById('d-avg').innerText = Math.round(d.t_avg) + ' C';
      document.getElementById('d-ext-avg').innerText = (d.t_ext_avg !== null) ? (Math.round(d.t_ext_avg) + ' C') : '-- C';
      document.getElementById('d-min-max').innerText = Math.round(d.t_min) + ' C / ' + Math.round(d.t_max) + ' C';
      document.getElementById('d-delta').innerText = Math.round(d.delta_t) + ' C';
      document.getElementById('d-uavg').innerText = Math.round(d.u_avg) + ' %';

      var grid = document.getElementById('hours-grid');
      grid.innerHTML = '';

      d.horas.forEach(function(h) {
        var box = document.createElement('div');
        var hasData = h.leituras > 0;
        box.className = 'hour-box ' + (hasData ? 'has-data' : 'no-data');
        var extHtml = (h.t_ext_avg !== null) ? '<span class="hb-temp-ext">' + h.t_ext_avg + ' C</span>' : '';
        box.innerHTML = 
          '<div class="hb-time">' + h.hora + '</div>' +
          '<div class="hb-temps-row">' +
            '<span class="hb-temp-int">' + (hasData ? (h.t_avg + ' C') : '--') + '</span>' +
            extHtml +
          '</div>' +
          '<div class="hb-hum">' + (hasData ? (h.u_avg + ' %') : '--') + '</div>';
        grid.appendChild(box);
      });

      renderizarGraficoComparativo(d.horas, d.data_formatada);
    }

    function navegarData(direcao) {
      if (!diaSelecionado || datasDisponiveis.length === 0) return;
      var idx = datasDisponiveis.indexOf(diaSelecionado);
      if (idx !== -1) {
        var novoIdx = idx + direcao;
        if (novoIdx >= 0 && novoIdx < datasDisponiveis.length) {
          selecionarDataCalendario(datasDisponiveis[novoIdx]);
          return;
        }
      }
      var dAtual = new Date(diaSelecionado + 'T12:00:00');
      dAtual.setDate(dAtual.getDate() + direcao);
      var iso = dAtual.toISOString().split('T')[0];
      selecionarDataCalendario(iso);
    }

    function selecionarHoje() {
      if (datasDisponiveis.length > 0) {
        selecionarDataCalendario(datasDisponiveis[datasDisponiveis.length - 1]);
      }
    }

    function renderizarGraficoComparativo(horas, dataFormatada) {
      var svg = document.getElementById('svg-chart');
      document.getElementById('chart-title').innerText = 'Curva Termica: Interna x Externa - ' + dataFormatada;

      var pontosInt = horas.filter(function(h) { return h.leituras > 0; });
      var pontosExt = horas.filter(function(h) { return h.t_ext_avg !== null; });

      if (pontosInt.length === 0) {
        svg.innerHTML = '<text x="50%" y="50%" text-anchor="middle" fill="#94a3b8" font-size="12">Sem dados registrados.</text>';
        return;
      }

      document.getElementById('legend-ext-item').style.display = (pontosExt.length > 0) ? 'flex' : 'none';

      var todasTemps = [];
      pontosInt.forEach(function(p) { todasTemps.push(p.t_min); todasTemps.push(p.t_max); });
      pontosExt.forEach(function(p) { todasTemps.push(p.t_ext_avg); });

      var minT = Math.floor(Math.min.apply(null, todasTemps)) - 1;
      var maxT = Math.ceil(Math.max.apply(null, todasTemps)) + 1;
      var tRange = (maxT - minT) || 1;

      var W = 900;
      var H = 200;
      var padL = 35;
      var padR = 20;
      var padT = 20;
      var padB = 25;

      var chartW = W - padL - padR;
      var chartH = H - padT - padB;

      function getX(hNum) { return padL + (hNum / 23) * chartW; }
      function getY(temp) { return padT + chartH - ((temp - minT) / tRange) * chartH; }

      var svgHTML = 
        '<defs>' +
          '<linearGradient id="gradInt" x1="0" y1="0" x2="0" y2="1">' +
            '<stop offset="0%" stop-color="#0284c7" stop-opacity="0.18"/>' +
            '<stop offset="100%" stop-color="#0284c7" stop-opacity="0.01"/>' +
          '</linearGradient>' +
          '<linearGradient id="gradExt" x1="0" y1="0" x2="0" y2="1">' +
            '<stop offset="0%" stop-color="#ea580c" stop-opacity="0.12"/>' +
            '<stop offset="100%" stop-color="#ea580c" stop-opacity="0.01"/>' +
          '</linearGradient>' +
        '</defs>';

      var steps = 3;
      for (var i = 0; i <= steps; i++) {
        var val = Math.round(minT + (tRange * i) / steps);
        var y = getY(val);
        svgHTML += '<line x1="' + padL + '" y1="' + y + '" x2="' + (W - padR) + '" y2="' + y + '" stroke="#f1f5f9" stroke-width="1"/>' +
                   '<text x="' + (padL - 8) + '" y="' + (y + 4) + '" fill="#94a3b8" font-size="10" text-anchor="end" font-weight="600">' + val + ' C</text>';
      }

      [0, 4, 8, 12, 16, 20, 23].forEach(function(h) {
        var x = getX(h);
        svgHTML += '<text x="' + x + '" y="' + (H - 5) + '" fill="#94a3b8" font-size="10" text-anchor="middle" font-weight="600">' + String(h).padStart(2,'0') + 'h</text>';
      });

      if (pontosExt.length > 0) {
        var pathExt = '';
        var areaExt = '';
        pontosExt.forEach(function(p, idx) {
          var x = getX(p.h_num);
          var y = getY(p.t_ext_avg);
          if (idx === 0) {
            pathExt += 'M ' + x + ' ' + y;
            areaExt += 'M ' + x + ' ' + (H - padB) + ' L ' + x + ' ' + y;
          } else {
            pathExt += ' L ' + x + ' ' + y;
            areaExt += ' L ' + x + ' ' + y;
          }
        });
        var ultExt = pontosExt[pontosExt.length - 1];
        areaExt += ' L ' + getX(ultExt.h_num) + ' ' + (H - padB) + ' Z';

        svgHTML += '<path d="' + areaExt + '" fill="url(#gradExt)"/>';
        svgHTML += '<path d="' + pathExt + '" fill="none" stroke="#ea580c" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"/>';

        pontosExt.forEach(function(p) {
          var x = getX(p.h_num);
          var y = getY(p.t_ext_avg);
          svgHTML += '<circle cx="' + x + '" cy="' + y + '" r="3.5" fill="#ffffff" stroke="#ea580c" stroke-width="2"/>';
        });
      }

      var pathInt = '';
      var areaInt = '';
      pontosInt.forEach(function(p, idx) {
        var x = getX(p.h_num);
        var y = getY(p.t_avg);
        if (idx === 0) {
          pathInt += 'M ' + x + ' ' + y;
          areaInt += 'M ' + x + ' ' + (H - padB) + ' L ' + x + ' ' + y;
        } else {
          pathInt += ' L ' + x + ' ' + y;
          areaInt += ' L ' + x + ' ' + y;
        }
      });
      var ultInt = pontosInt[pontosInt.length - 1];
      areaInt += ' L ' + getX(ultInt.h_num) + ' ' + (H - padB) + ' Z';

      svgHTML += '<path d="' + areaInt + '" fill="url(#gradInt)"/>';
      svgHTML += '<path d="' + pathInt + '" fill="none" stroke="#0284c7" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"/>';

      pontosInt.forEach(function(p) {
        var x = getX(p.h_num);
        var y = getY(p.t_avg);
        svgHTML += '<circle cx="' + x + '" cy="' + y + '" r="4" fill="#ffffff" stroke="#0284c7" stroke-width="2" class="chart-dot"' +
                   ' data-hora="' + p.hora + '" data-int="' + p.t_avg + '" data-ext="' + (p.t_ext_avg !== null ? p.t_ext_avg : '--') + '" data-hum="' + p.u_avg + '"/>';
      });

      svg.setAttribute('viewBox', '0 0 ' + W + ' ' + H);
      svg.innerHTML = svgHTML;

      var tooltip = document.getElementById('chart-tooltip');
      document.querySelectorAll('.chart-dot').forEach(function(dot) {
        dot.addEventListener('mouseenter', function() {
          var hora = dot.getAttribute('data-hora');
          var tI = dot.getAttribute('data-int');
          var tE = dot.getAttribute('data-ext');
          var hum = dot.getAttribute('data-hum');
          var extTxt = (tE !== '--') ? ' • Externa: <span style="color:#fdba74;">' + tE + ' C</span>' : '';
          tooltip.innerHTML = '<b>' + hora + '</b> — Interna: <span style="color:#38bdf8;">' + tI + ' C</span>' + extTxt + ' (' + hum + '%)';
          tooltip.style.display = 'block';
        });
        dot.addEventListener('mousemove', function(e) {
          var rect = document.getElementById('svg-chart-wrapper').getBoundingClientRect();
          tooltip.style.left = (e.clientX - rect.left + 10) + 'px';
          tooltip.style.top = (e.clientY - rect.top - 30) + 'px';
        });
        dot.addEventListener('mouseleave', function() {
          tooltip.style.display = 'none';
        });
      });
    }

    var horaServidor = null;
    function relogioTick() {
      if (horaServidor) {
        horaServidor.setSeconds(horaServidor.getSeconds() + 1);
        var h = String(horaServidor.getHours()).padStart(2, '0');
        var m = String(horaServidor.getMinutes()).padStart(2, '0');
        var s = String(horaServidor.getSeconds()).padStart(2, '0');
        document.getElementById('relogio-visor').innerText = h + ':' + m + ':' + s;
      } else {
        var d = new Date();
        document.getElementById('relogio-visor').innerText = d.toLocaleTimeString('pt-BR');
      }
    }
    setInterval(relogioTick, 1000);

    async function atualizarDados() {
      try {
        var res = await fetch('/api/dados');
        if (!res.ok) return;
        var d = await res.json();

        if (d.data_atual && d.hora_atual && d.hora_atual !== '--:--:--') {
          var partesData = d.data_atual.split('/');
          var partesHora = d.hora_atual.split(':');
          if (partesData.length === 3 && partesHora.length === 3) {
            horaServidor = new Date(partesData[2], partesData[1]-1, partesData[0], partesHora[0], partesHora[1], partesHora[2]);
          }
        }

        // Interno
        var tInt = Math.round(d.interno.temperatura);
        var uInt = Math.round(d.interno.umidade);
        document.getElementById('temp-int').innerText = tInt;
        document.getElementById('umid-int').innerText = 'Umidade: ' + uInt + ' %';

        var conf = "Agradavel";
        if (tInt >= 28) conf = "Calor";
        else if (tInt <= 18) conf = "Frio";
        document.getElementById('conforto').innerText = conf;

        // Externo
        var tExt = Math.round(d.externo.temperatura);
        var uExt = Math.round(d.externo.umidade);
        document.getElementById('temp-ext').innerText = tExt;
        document.getElementById('umid-ext').innerText = 'Umidade: ' + uExt + ' % • ' + (d.externo.condicao || '');
        document.getElementById('max-min').innerText = 'Min ' + Math.round(d.externo.temp_min) + ' C / Max ' + Math.round(d.externo.temp_max) + ' C';
        document.getElementById('prob-chuva').innerText = 'Chuva ' + (d.externo.probabilidade_chuva || 0) + '%';

        // Comparativo
        var diff = tInt - tExt;
        var absDiff = Math.abs(diff);
        var diffBanner = document.getElementById('diff-banner');
        if (diff > 0) {
          diffBanner.innerText = 'O ambiente interno esta ' + absDiff + ' C mais quente que o exterior.';
        } else if (diff < 0) {
          diffBanner.innerText = 'O ambiente interno esta ' + absDiff + ' C mais fresco que o exterior.';
        } else {
          diffBanner.innerText = 'As temperaturas interna e externa estao iguais.';
        }

        // Medias Consolidadas
        if (d.historico) {
          if (d.historico.media_diaria > 0) document.getElementById('med-hoje').innerText = Math.round(d.historico.media_diaria) + ' C';
          if (d.historico.media_semanal > 0) document.getElementById('med-sem').innerText = Math.round(d.historico.media_semanal) + ' C';
          if (d.historico.media_mensal > 0) document.getElementById('med-mes').innerText = Math.round(d.historico.media_mensal) + ' C';

          var r = d.historico.recap;
          if (r && r.data && r.data !== '--') {
            var pR = r.data.split('-');
            var dataRecapBR = (pR.length === 3) ? (pR[2] + '/' + pR[1] + '/' + pR[0]) : r.data;
            document.getElementById('recap-texto').innerHTML = 
              'No dia <b>' + dataRecapBR + '</b> ocorreu a maior oscilacao registrada no ambiente interno: ' +
              'variacao termica de <b>' + Math.round(r.delta_temp) + ' C</b> (minima de ' + Math.round(r.temp_min) + ' C e maxima de ' + Math.round(r.temp_max) + ' C) ' +
              'e variacao de umidade de <b>' + Math.round(r.delta_umid) + '%</b>.';
          }
        }

        // Telemetria do ESP32
        if (d.sistema) {
          if (d.sistema.temp_chip !== undefined) document.getElementById('sys-temp-chip').innerText = Math.round(d.sistema.temp_chip) + ' C';
          if (d.sistema.ram_livre_kb !== undefined) document.getElementById('sys-ram').innerText = d.sistema.ram_livre_kb + ' KB';
          if (d.sistema.sinal_wifi_dbm !== undefined) document.getElementById('sys-wifi').innerText = d.sistema.sinal_wifi_dbm + ' dBm';
          if (d.sistema.uptime_segundos !== undefined) {
            var s = d.sistema.uptime_segundos;
            var h = Math.floor(s / 3600);
            var m = Math.floor((s % 3600) / 60);
            document.getElementById('sys-uptime').innerText = h + 'h ' + m + 'm';
          }
        }
      } catch (e) {
        console.error("Erro dados ao vivo:", e);
      }
    }

    async function falarAlexa() {
      var texto = document.getElementById('msg-alexa').value;
      try {
        var res = await fetch('/api/falar-alexa', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ texto: texto })
        });
        var json = await res.json();
        alert(json.mensagem || "Mensagem enviada para a Alexa!");
      } catch (e) {
        alert("Erro ao enviar para Alexa: " + e);
      }
    }

    async function testarNtfy() {
      try {
        var res = await fetch('/api/testar-ntfy', { method: 'POST' });
        var json = await res.json();
        alert(json.mensagem || "Alerta urgente enviado para o NTFY!");
      } catch (e) {
        alert("Erro ao disparar NTFY: " + e);
      }
    }

    setInterval(atualizarDados, 3000);
    atualizarDados();
    carregarHistorico();
  </script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

// =========================================================================
// SETUP
// =========================================================================
void setup() {
  Serial.begin(115200);
  dht.begin();

  // Carrega configuracoes da memoria NVS
  prefs.begin("clima_cfg", false);
  cepConfig = prefs.getString("cep", "01001000");
  localizacaoNome = prefs.getString("local", "Sao Paulo");
  latLocal = prefs.getFloat("lat", -23.5505);
  lonLocal = prefs.getFloat("lon", -46.6333);

  horaAviso = prefs.getInt("hora", 8);
  minutoAviso = prefs.getInt("minuto", 0);
  avisoAtivo = prefs.getBool("ativo", true);
  ntfyTopic = prefs.getString("ntfy", "seu-topico-ntfy");
  vmToken = prefs.getString("vm_token", "SEU_TOKEN_VOICE_MONKEY");
  vmDevice = prefs.getString("vm_device", "seu-dispositivo-alexa");

  falaBase = prefs.getString("f_base", "Bom dia, a temperatura atualmente e {temp_in} graus, la fora esta {temp_ext} graus com {condicao}.");
  falaChuva = prefs.getString("f_chuva", "Nao esqueca o guarda-chuva.");
  falaCalor = prefs.getString("f_calor", "Vai fazer calor, beba bastante agua.");
  falaFrio = prefs.getString("f_frio", "Leve um agasalho, vai esfriar.");
  falaSeco = prefs.getString("f_seco", "O ar esta seco, hidrate-se bem.");
  falaBom = prefs.getString("f_bom", "Tenha um excelente dia.");

  Serial.println("\n=============================================");
  Serial.println("  ESTACAO METEOROLOGICA");
  Serial.println("=============================================");
  Serial.printf("CEP: %s | Local: %s\n", cepConfig.c_str(), localizacaoNome.c_str());
  Serial.printf("Aviso Diario Agendado: %02d:%02d | Ativo: %s\n", horaAviso, minutoAviso, avisoAtivo ? "SIM" : "NAO");
  Serial.printf("NTFY: %s | Alexa Device: %s\n", ntfyTopic.c_str(), vmDevice.c_str());

  Serial.print("Conectando ao Wi-Fi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWi-Fi Conectado com sucesso!");
  Serial.print("Painel Web disponivel em: http://");
  Serial.println(WiFi.localIP());

  // Sincronizacao NTP (fuso UTC-3 Brasilia)
  configTime(-3 * 3600, 0, "pool.ntp.br", "a.st1.ntp.br", "time.google.com");

  // Primeira consulta de previsao externa
  atualizarPrevisaoExterna();
  ultimoTempoPrevisao = millis();

  // Rotas do Servidor Web
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/dados", HTTP_GET, handleApiDados);
  server.on("/api/historico", handleApiHistorico);
  server.on("/api/historico-dia", HTTP_GET, handleApiHistoricoDia);
  server.on("/api/config", HTTP_POST, handleSalvarConfig);
  server.on("/api/falar-alexa", HTTP_POST, handleFalarAlexa);
  server.on("/api/testar-ntfy", HTTP_POST, handleTestarNtfy);

  server.begin();
  Serial.println("Servidor Web pronto e operando!");
}

// =========================================================================
// LOOP PRINCIPAL
// =========================================================================
void loop() {
  server.handleClient();

  // 1. Leitura do Sensor DHT11 a cada 2 segundos
  if (millis() - ultimoTempoDHT >= 2000) {
    ultimoTempoDHT = millis();
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) {
      tempInterna = t;
      umidInterna = h;

      // Acumula para media diaria local
      somaTempLocal += t;
      contagemTempLocal++;
      if (contagemTempLocal > 86400) { // Protecao contra overflow apos muitas semanas
        somaTempLocal = t;
        contagemTempLocal = 1;
      }
    }
  }

  // 2. Previsao Externa atualizada a cada 30 minutos (economizando recursos)
  if (millis() - ultimoTempoPrevisao >= INTERVALO_PREVISAO) {
    ultimoTempoPrevisao = millis();
    atualizarPrevisaoExterna();
  }

  // 3. Verificacao do Horario para Avisos Diarios Contextuais
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    if (avisoAtivo &&
        timeinfo.tm_hour == horaAviso &&
        timeinfo.tm_min == minutoAviso &&
        timeinfo.tm_mday != ultimoDiaAvisoEnviado) {
      ultimoDiaAvisoEnviado = timeinfo.tm_mday;
      Serial.println("[Agendador] Disparando aviso diario agendado!");
      enviarAvisos("", true, true);
    }
  }
}
