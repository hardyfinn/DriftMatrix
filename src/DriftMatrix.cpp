// DriftMatrix: a 32 x 32 matrix mixer whose matrix you never see.
//
// How it works, in musical terms:
// - Every input can reach every output through a "crosspoint" (32 x 32 = 1024
//   of them). Each crosspoint holds two hidden dice rolls: one decides whether
//   it is active, the other decides its level.
// - CHANCE is how likely each crosspoint is to be active. Because the dice are
//   kept, turning CHANCE up adds crosspoints one by one and turning it down
//   removes them again in reverse order: the matrix doesn't jump about.
// - MODE: ON/OFF makes every active crosspoint full level. 0-100% uses the
//   level dice instead, so each active crosspoint has its own random level.
// - RESEED rolls new dice. RADICAL is how many crosspoints get new dice:
//   100% = a whole new matrix, 5% = a small mutation of the current one.
// - SLEW smooths every change (reseed, CHANCE, snapshot mixing) so audio
//   doesn't click, or so CV routings glide from one to the next.
// - Snapshots: STORE then a slot button saves the current matrix (its dice).
//   Tapping a slot on its own loads it back as the LIVE matrix.
//   A and B pick two matrices (LIVE or slots 1-8) and MIX fades between them,
//   either SMOOTH (every level crossfades) or COIN (each crosspoint jumps from
//   A to B at its own point as MIX turns).
// - GAIN AUTO divides each output by how much is piled onto it, so it never
//   gets louder than a single full-level input. SUM just adds.
#include "plugin.hpp"

// ---- LAYOUT (same numbers in res/make_panel.py) ----
static const float W = 182.88f;  // 36 HP
static const float DISP_X = 8.f, DISP_Y = 18.5f, DISP_H = 10.5f;
static const float Y1 = 42.f;
static const float X_MODE = 14.f, X_CHANCE = 37.f, X_CHANCECV = 51.f, X_RADICAL = 66.f, X_RADICALCV = 79.f,
                   X_RESEED = 96.f, X_TRIG = 108.f, X_SLEW = 125.f, X_SLEWCV = 138.f, X_GAIN = 158.f;
static const float Y2 = 60.5f;
static const float X_STORE = 13.f, X_SLOT0 = 25.f, SLOT_DX = 8.4f;
static const float X_A = 98.f, X_B = 111.f, X_MIX = 126.f, X_MIXMODE = 140.f, X_MIXCV = 154.f, X_ABCV = 168.f;
static const float JACK_ROW_Y[4] = {80.5f, 91.6f, 102.7f, 113.8f};
static const float X_IN0 = 14.1f, X_OUT0 = 100.04f, JACK_DX = 9.6f;

static const int N = 32;
static const int NN = N * N;
static const int SLOTS = 8;

struct OrangeLight : GrayModuleLightWidget {
	OrangeLight() { addBaseColor(nvgRGB(0xef, 0x7a, 0x37)); }
};

// The hidden dice for a whole matrix
struct Dice {
	float act[NN];   // crosspoint active if act < CHANCE
	float lvl[NN];   // level in 0-100% mode
	void roll() {
		for (int k = 0; k < NN; k++) {
			act[k] = random::uniform();
			lvl[k] = random::uniform();
		}
	}
};

static std::string encode(const float* v, int n) {
	static const char* hex = "0123456789abcdef";
	std::string s(n * 4, '0');
	for (int k = 0; k < n; k++) {
		int q = clamp((int) (v[k] * 65536.f), 0, 65535);
		for (int d = 0; d < 4; d++)
			s[k * 4 + d] = hex[(q >> (12 - 4 * d)) & 15];
	}
	return s;
}

static bool decode(const char* s, float* v, int n) {
	if (!s || (int) strlen(s) != n * 4)
		return false;
	for (int k = 0; k < n; k++) {
		int q = 0;
		for (int d = 0; d < 4; d++) {
			char c = s[k * 4 + d];
			int h = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : 0;
			q = (q << 4) | h;
		}
		v[k] = (q + 0.5f) / 65536.f;
	}
	return true;
}

struct DriftMatrix : Module {
	enum ParamIds {
		MODE_PARAM,
		CHANCE_PARAM,
		RADICAL_PARAM,
		RESEED_PARAM,
		SLEW_PARAM,
		GAIN_PARAM,
		STORE_PARAM,
		ENUMS(SLOT_PARAM, SLOTS),
		A_PARAM,
		B_PARAM,
		MIX_PARAM,
		MIXMODE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		ENUMS(SIG_INPUT, N),
		CHANCE_INPUT,
		RADICAL_INPUT,
		RESEED_INPUT,
		SLEW_INPUT,
		MIX_INPUT,
		AB_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(SIG_OUTPUT, N),
		NUM_OUTPUTS
	};
	enum LightIds {
		RESEED_LIGHT,
		STORE_LIGHT,
		ENUMS(SLOT_LIGHT, SLOTS * 3),
		NUM_LIGHTS
	};

	Dice live;
	float flip[NN];           // coin-flip point of each crosspoint for COIN mixing
	Dice snap[SLOTS];
	bool full[SLOTS] = {};

	float target[NN] = {};    // where each crosspoint gain is heading
	float gain[NN] = {};      // the smoothed gain actually used
	float coef = 1.f;
	bool armed = false;
	bool softClip = false;

	// shown on the display
	int shownActive = 0, shownIns = 0, shownOuts = 0, shownA = 0, shownB = 1;
	float shownChance = 0.f, shownMix = 0.f;

	dsp::SchmittTrigger reseedTrig, reseedButton, storeButton, slotButton[SLOTS];
	dsp::PulseGenerator reseedFlash;
	dsp::ClockDivider controlDivider, lightDivider;
	float blink = 0.f;

	DriftMatrix() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configSwitch(MODE_PARAM, 0.f, 1.f, 0.f, "Mode", {"0-100% (random level per crosspoint)", "On/off (full level)"});
		configParam(CHANCE_PARAM, 0.f, 1.f, 0.25f, "Chance a crosspoint is active", "%", 0.f, 100.f);
		configParam(RADICAL_PARAM, 0.f, 1.f, 1.f, "Radical (how much a reseed changes)", "%", 0.f, 100.f);
		configButton(RESEED_PARAM, "Reseed");
		configParam(SLEW_PARAM, 0.f, 1.f, 0.2f, "Slew", " ms", 2000.f, 1.f);
		configSwitch(GAIN_PARAM, 0.f, 1.f, 1.f, "Gain", {"Sum (plain add)", "Auto (keeps each output at single-input level)"});
		configButton(STORE_PARAM, "Store (then tap a slot)");
		for (int s = 0; s < SLOTS; s++)
			configButton(SLOT_PARAM + s, string::f("Slot %d (after STORE: save here; otherwise: load into LIVE)", s + 1));
		configSwitch(A_PARAM, 0.f, 8.f, 0.f, "A", {"Live", "Slot 1", "Slot 2", "Slot 3", "Slot 4", "Slot 5", "Slot 6", "Slot 7", "Slot 8"});
		configSwitch(B_PARAM, 0.f, 8.f, 1.f, "B", {"Live", "Slot 1", "Slot 2", "Slot 3", "Slot 4", "Slot 5", "Slot 6", "Slot 7", "Slot 8"});
		configParam(MIX_PARAM, 0.f, 1.f, 0.f, "Mix A to B", "%", 0.f, 100.f);
		configSwitch(MIXMODE_PARAM, 0.f, 1.f, 1.f, "A-B mix", {"Coin flip (each crosspoint jumps at its own point)", "Smooth (every level crossfades)"});
		for (int p : {MODE_PARAM, GAIN_PARAM, A_PARAM, B_PARAM, MIXMODE_PARAM, SLEW_PARAM})
			getParamQuantity(p)->randomizeEnabled = false;

		for (int i = 0; i < N; i++) {
			configInput(SIG_INPUT + i, string::f("In %d", i + 1));
			configOutput(SIG_OUTPUT + i, string::f("Out %d", i + 1));
		}
		configInput(CHANCE_INPUT, "Chance CV (+1 V = +10 %)");
		configInput(RADICAL_INPUT, "Radical CV (+1 V = +10 %)");
		configInput(RESEED_INPUT, "Reseed trigger");
		configInput(SLEW_INPUT, "Slew CV (+1 V = +10 %)");
		configInput(MIX_INPUT, "Mix CV (+1 V = +10 %)");
		configInput(AB_INPUT, "A/B CV (+1 V = both A and B step one place on)");

		controlDivider.setDivision(16);
		lightDivider.setDivision(256);
		live.roll();
		rollFlip(1.f);
	}

	void rollFlip(float amount) {
		for (int k = 0; k < NN; k++)
			if (amount >= 1.f || random::uniform() < amount)
				flip[k] = random::uniform();
	}

	void reseed(float radical) {
		for (int k = 0; k < NN; k++) {
			if (radical >= 1.f || random::uniform() < radical) {
				live.act[k] = random::uniform();
				live.lvl[k] = random::uniform();
				flip[k] = random::uniform();
			}
		}
		reseedFlash.trigger(0.15f);
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		live.roll();
		rollFlip(1.f);
		for (int s = 0; s < SLOTS; s++)
			full[s] = false;
		armed = false;
	}

	void onRandomize(const RandomizeEvent& e) override {
		Module::onRandomize(e);
		reseed(1.f);
	}

	// which matrix (0 = live, 1-8 = slot) A or B points at, with the A/B CV
	int pick(int base) {
		int off = (int) std::round(inputs[AB_INPUT].getVoltage());
		return ((base + off) % 9 + 9) % 9;
	}

	const Dice* source(int sel) {
		if (sel == 0) return &live;
		return full[sel - 1] ? &snap[sel - 1] : nullptr;
	}

	void control(const ProcessArgs& args) {
		float chance = clamp(params[CHANCE_PARAM].getValue() + inputs[CHANCE_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		float mix = clamp(params[MIX_PARAM].getValue() + inputs[MIX_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		bool onoff = params[MODE_PARAM].getValue() > 0.5f;
		bool smooth = params[MIXMODE_PARAM].getValue() > 0.5f;
		int a = pick((int) params[A_PARAM].getValue());
		int b = pick((int) params[B_PARAM].getValue());
		const Dice* da = source(a);
		const Dice* db = source(b);

		int active = 0;
		for (int k = 0; k < NN; k++) {
			float ga = (da && da->act[k] < chance) ? (onoff ? 1.f : da->lvl[k]) : 0.f;
			float gb = (db && db->act[k] < chance) ? (onoff ? 1.f : db->lvl[k]) : 0.f;
			float t;
			if (smooth)
				t = ga + (gb - ga) * mix;
			else
				t = (flip[k] < mix) ? gb : ga;
			target[k] = t;
			if (t > 0.001f) active++;
		}

		// slew: 1 ms .. 2 s, exponential knob
		float slew = clamp(params[SLEW_PARAM].getValue() + inputs[SLEW_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		float tau = 0.001f * std::pow(2000.f, slew);
		coef = 1.f - std::exp(-1.f / (tau * args.sampleRate));

		shownActive = active;
		shownChance = chance;
		shownMix = mix;
		shownA = a;
		shownB = b;
	}

	void process(const ProcessArgs& args) override {
		// ---- buttons and triggers
		float radical = clamp(params[RADICAL_PARAM].getValue() + inputs[RADICAL_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		bool doReseed = reseedButton.process(params[RESEED_PARAM].getValue());
		if (reseedTrig.process(inputs[RESEED_INPUT].getVoltage(), 0.1f, 1.f))
			doReseed = true;
		if (doReseed)
			reseed(radical);
		if (storeButton.process(params[STORE_PARAM].getValue()))
			armed = !armed;
		for (int s = 0; s < SLOTS; s++) {
			if (slotButton[s].process(params[SLOT_PARAM + s].getValue())) {
				if (armed) {
					snap[s] = live;
					full[s] = true;
					armed = false;
				}
				else if (full[s]) {
					live = snap[s];
				}
			}
		}

		if (controlDivider.process())
			control(args);

		// ---- which jacks are patched
		float x[N];
		int ins[N];
		int nIn = 0;
		for (int i = 0; i < N; i++) {
			if (inputs[SIG_INPUT + i].isConnected()) {
				x[nIn] = inputs[SIG_INPUT + i].getVoltage();
				ins[nIn++] = i;
			}
		}

		// ---- smooth every crosspoint, then mix the patched ones
		for (int k = 0; k < NN; k++)
			gain[k] += (target[k] - gain[k]) * coef;

		bool autoGain = params[GAIN_PARAM].getValue() > 0.5f;
		int nOut = 0;
		for (int o = 0; o < N; o++) {
			if (!outputs[SIG_OUTPUT + o].isConnected())
				continue;
			nOut++;
			const float* g = &gain[o * N];
			float sum = 0.f, gsum = 0.f;
			for (int j = 0; j < nIn; j++) {
				float gg = g[ins[j]];
				sum += gg * x[j];
				gsum += gg;
			}
			if (autoGain && gsum > 1.f)
				sum /= gsum;
			if (softClip)
				sum = 10.f * std::tanh(sum * 0.1f);
			outputs[SIG_OUTPUT + o].setVoltage(sum);
		}

		// ---- lights
		bool flash = reseedFlash.process(args.sampleTime);
		if (lightDivider.process()) {
			float dt = args.sampleTime * lightDivider.getDivision();
			shownIns = nIn;
			shownOuts = nOut;
			blink += dt * 3.f;
			if (blink >= 1.f) blink -= 1.f;
			bool blinkOn = blink < 0.5f;
			lights[RESEED_LIGHT].setBrightnessSmooth(flash ? 1.f : 0.f, dt, 20.f);
			lights[STORE_LIGHT].setBrightness(armed && blinkOn ? 1.f : 0.f);
			for (int s = 0; s < SLOTS; s++) {
				float r = 0.f, gg = 0.f, bb = 0.f;
				bool isA = shownA == s + 1, isB = shownB == s + 1;
				if (isA && isB) { r = 0.9f; gg = 0.9f; bb = 0.9f; }
				else if (isA) { r = 1.f; gg = 0.42f; bb = 0.08f; }
				else if (isB) { r = 0.1f; gg = 0.78f; bb = 0.9f; }
				else if (full[s]) { r = gg = bb = 0.16f; }
				if (armed) {
					// waiting for a slot: every slot pulses white
					float w = blinkOn ? 0.7f : 0.1f;
					r = gg = bb = w;
				}
				lights[SLOT_LIGHT + s * 3 + 0].setBrightness(r);
				lights[SLOT_LIGHT + s * 3 + 1].setBrightness(gg);
				lights[SLOT_LIGHT + s * 3 + 2].setBrightness(bb);
			}
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "liveAct", json_string(encode(live.act, NN).c_str()));
		json_object_set_new(root, "liveLvl", json_string(encode(live.lvl, NN).c_str()));
		json_object_set_new(root, "flip", json_string(encode(flip, NN).c_str()));
		json_t* slots = json_array();
		for (int s = 0; s < SLOTS; s++) {
			json_t* js = json_object();
			json_object_set_new(js, "full", json_boolean(full[s]));
			if (full[s]) {
				json_object_set_new(js, "act", json_string(encode(snap[s].act, NN).c_str()));
				json_object_set_new(js, "lvl", json_string(encode(snap[s].lvl, NN).c_str()));
			}
			json_array_append_new(slots, js);
		}
		json_object_set_new(root, "slots", slots);
		json_object_set_new(root, "softClip", json_boolean(softClip));
		return root;
	}

	void dataFromJson(json_t* root) override {
		json_t* j;
		if ((j = json_object_get(root, "liveAct"))) decode(json_string_value(j), live.act, NN);
		if ((j = json_object_get(root, "liveLvl"))) decode(json_string_value(j), live.lvl, NN);
		if ((j = json_object_get(root, "flip"))) decode(json_string_value(j), flip, NN);
		if ((j = json_object_get(root, "slots"))) {
			for (int s = 0; s < SLOTS && s < (int) json_array_size(j); s++) {
				json_t* js = json_array_get(j, s);
				json_t* f = json_object_get(js, "full");
				full[s] = f && json_boolean_value(f);
				if (full[s]) {
					json_t* ja = json_object_get(js, "act");
					json_t* jl = json_object_get(js, "lvl");
					full[s] = ja && jl && decode(json_string_value(ja), snap[s].act, NN) && decode(json_string_value(jl), snap[s].lvl, NN);
				}
			}
		}
		if ((j = json_object_get(root, "softClip"))) softClip = json_boolean_value(j);
	}
};

static std::string matrixName(int sel) {
	return sel == 0 ? "LIVE" : string::f("%d", sel);
}

struct MatrixDisplay : Widget {
	DriftMatrix* module = nullptr;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1)
			return;
		std::shared_ptr<Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (!font)
			return;
		nvgFontFaceId(args.vg, font->handle);
		float h = box.size.y;
		float pad = mm2px(2.6f);

		std::string l1, l2, r1, r2;
		if (module) {
			bool onoff = module->params[DriftMatrix::MODE_PARAM].getValue() > 0.5f;
			bool smooth = module->params[DriftMatrix::MIXMODE_PARAM].getValue() > 0.5f;
			l1 = string::f("%s  chance %d%%  %d of 1024 live", onoff ? "ON/OFF" : "0-100%",
				(int) std::round(module->shownChance * 100.f), module->shownActive);
			l2 = string::f("%d ins > %d outs patched", module->shownIns, module->shownOuts);
			if (module->armed)
				l2 = "STORE: tap a slot to save LIVE";
			auto name = [&](int sel) {
				std::string n = matrixName(sel);
				if (sel > 0 && !module->full[sel - 1]) n += " (empty)";
				return n;
			};
			r1 = "A " + name(module->shownA) + "  > B " + name(module->shownB);
			r2 = string::f("mix %d%%  %s", (int) std::round(module->shownMix * 100.f), smooth ? "smooth" : "coin");
		}
		else {
			l1 = "0-100%  chance 25%  256 of 1024 live";
			l2 = "32 x 32, unseen";
			r1 = "A LIVE  > B 1";
			r2 = "mix 0%  smooth";
		}
		nvgFontSize(args.vg, mm2px(3.1f));
		nvgTextLetterSpacing(args.vg, 0.f);
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		nvgFillColor(args.vg, nvgRGB(0xf3, 0xef, 0xe8));
		nvgText(args.vg, pad, h * 0.44f, l1.c_str(), NULL);
		nvgFillColor(args.vg, nvgRGB(0xb9, 0xb3, 0xa9));
		nvgFontSize(args.vg, mm2px(2.8f));
		nvgText(args.vg, pad, h * 0.84f, l2.c_str(), NULL);
		nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
		nvgFontSize(args.vg, mm2px(3.1f));
		nvgFillColor(args.vg, nvgRGB(0xef, 0x7a, 0x37));
		nvgText(args.vg, box.size.x - pad, h * 0.44f, r1.c_str(), NULL);
		nvgFontSize(args.vg, mm2px(2.8f));
		nvgFillColor(args.vg, nvgRGB(0x3f, 0xd0, 0xe0));
		nvgText(args.vg, box.size.x - pad, h * 0.84f, r2.c_str(), NULL);
	}
};

struct DriftMatrixWidget : ModuleWidget {
	DriftMatrixWidget(DriftMatrix* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/DriftMatrix.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		MatrixDisplay* disp = new MatrixDisplay;
		disp->module = module;
		disp->box.pos = mm2px(Vec(DISP_X, DISP_Y));
		disp->box.size = mm2px(Vec(W - 2 * DISP_X, DISP_H));
		addChild(disp);

		// row 1
		addParam(createParamCentered<CKSS>(mm2px(Vec(X_MODE, Y1)), module, DriftMatrix::MODE_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(X_CHANCE, Y1)), module, DriftMatrix::CHANCE_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_CHANCECV, Y1 + 1.f)), module, DriftMatrix::CHANCE_INPUT));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X_RADICAL, Y1)), module, DriftMatrix::RADICAL_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_RADICALCV, Y1 + 1.f)), module, DriftMatrix::RADICAL_INPUT));
		addParam(createLightParamCentered<VCVLightBezel<OrangeLight>>(mm2px(Vec(X_RESEED, Y1)), module, DriftMatrix::RESEED_PARAM, DriftMatrix::RESEED_LIGHT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_TRIG, Y1 + 1.f)), module, DriftMatrix::RESEED_INPUT));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X_SLEW, Y1)), module, DriftMatrix::SLEW_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_SLEWCV, Y1 + 1.f)), module, DriftMatrix::SLEW_INPUT));
		addParam(createParamCentered<CKSS>(mm2px(Vec(X_GAIN, Y1)), module, DriftMatrix::GAIN_PARAM));

		// row 2
		addParam(createLightParamCentered<VCVLightBezel<WhiteLight>>(mm2px(Vec(X_STORE, Y2)), module, DriftMatrix::STORE_PARAM, DriftMatrix::STORE_LIGHT));
		for (int s = 0; s < SLOTS; s++)
			addParam(createLightParamCentered<VCVLightBezel<RedGreenBlueLight>>(mm2px(Vec(X_SLOT0 + s * SLOT_DX, Y2)), module, DriftMatrix::SLOT_PARAM + s, DriftMatrix::SLOT_LIGHT + s * 3));
		addParam(createParamCentered<RoundBlackSnapKnob>(mm2px(Vec(X_A, Y2)), module, DriftMatrix::A_PARAM));
		addParam(createParamCentered<RoundBlackSnapKnob>(mm2px(Vec(X_B, Y2)), module, DriftMatrix::B_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(X_MIX, Y2)), module, DriftMatrix::MIX_PARAM));
		addParam(createParamCentered<CKSS>(mm2px(Vec(X_MIXMODE, Y2)), module, DriftMatrix::MIXMODE_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_MIXCV, Y2 + 1.f)), module, DriftMatrix::MIX_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_ABCV, Y2 + 1.f)), module, DriftMatrix::AB_INPUT));

		// 32 in, 32 out
		for (int r = 0; r < 4; r++) {
			for (int c = 0; c < 8; c++) {
				int n = r * 8 + c;
				addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_IN0 + c * JACK_DX, JACK_ROW_Y[r])), module, DriftMatrix::SIG_INPUT + n));
				addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_OUT0 + c * JACK_DX, JACK_ROW_Y[r])), module, DriftMatrix::SIG_OUTPUT + n));
			}
		}
	}

	void appendContextMenu(Menu* menu) override {
		DriftMatrix* module = dynamic_cast<DriftMatrix*>(this->module);
		if (!module)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("Soft-clip outputs at about 10 V", "", &module->softClip));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Fill every empty slot with a fresh random matrix", "", [=]() {
			for (int s = 0; s < SLOTS; s++) {
				if (!module->full[s]) {
					module->snap[s].roll();
					module->full[s] = true;
				}
			}
		}));
		menu->addChild(createMenuItem("Clear all snapshots", "", [=]() {
			for (int s = 0; s < SLOTS; s++)
				module->full[s] = false;
		}));
	}
};

Model* modelDriftMatrix = createModel<DriftMatrix, DriftMatrixWidget>("DriftMatrix");
