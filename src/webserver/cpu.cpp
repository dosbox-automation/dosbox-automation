// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "webserver.h"
#include "bridge.h"
#include "private/cpu.h"

#include "base64/base64.h"
#include "http/http.h"
#include "json/json.h"

#include "cpu/cpu.h"
#include "cpu/registers.h"

#include <unordered_map>

using json = nlohmann::json;

namespace Webserver {

void Registers::load()
{
	this->eax   = reg_eax;
	this->ebx   = reg_ebx;
	this->ecx   = reg_ecx;
	this->edx   = reg_edx;
	this->esi   = reg_esi;
	this->edi   = reg_edi;
	this->esp   = reg_esp;
	this->ebp   = reg_ebp;
	this->eip   = reg_eip;
	this->flags = reg_flags;
	this->cs    = SegValue(SegNames::cs);
	this->ds    = SegValue(SegNames::ds);
	this->es    = SegValue(SegNames::es);
	this->ss    = SegValue(SegNames::ss);
	this->fs    = SegValue(SegNames::fs);
	this->gs    = SegValue(SegNames::gs);
}

void CpuStateCommand::Execute()
{
	regs.load();
	LOG_DEBUG("API: CpuStateCommand()");
}

void CpuStateCommand::Get(const httplib::Request&, httplib::Response& res)
{
	CpuStateCommand cmd;
	cmd.WaitForCompletion();

	json j;
	j["registers"] = cmd.regs;
	send_json(res, j);
}

RegisterRef RegisterKind(const std::string_view name)
{
	struct Entry {
		RegClass reg_class;
		int index;
	};

	static const std::unordered_map<std::string_view, Entry> lookup = {
	        {"eax", {RegClass::General, 0}},
	        {"ebx", {RegClass::General, 1}},
	        {"ecx", {RegClass::General, 2}},
	        {"edx", {RegClass::General, 3}},
	        {"esi", {RegClass::General, 4}},
	        {"edi", {RegClass::General, 5}},
	        {"esp", {RegClass::General, 6}},
	        {"ebp", {RegClass::General, 7}},
	        {"cs", {RegClass::Segment, 0}},
	        {"ds", {RegClass::Segment, 1}},
	        {"es", {RegClass::Segment, 2}},
	        {"ss", {RegClass::Segment, 3}},
	        {"fs", {RegClass::Segment, 4}},
	        {"gs", {RegClass::Segment, 5}},
	};

	auto it = lookup.find(name);
	if (it != lookup.end()) {
		return {it->second.reg_class, it->second.index};
	}
	return {RegClass::Unknown, -1};
}

void WriteRegisterCommand::Execute()
{
	auto ref = RegisterKind(name);

	if (ref.reg_class == RegClass::General) {
		// Map index to the lvalue reference
		uint32_t* regs[] = {&reg_eax, &reg_ebx, &reg_ecx, &reg_edx,
		                    &reg_esi, &reg_edi, &reg_esp, &reg_ebp};
		*regs[ref.index] = value;
	} else if (ref.reg_class == RegClass::Segment) {
		static constexpr SegNames seg_names[] = {
		        SegNames::cs, SegNames::ds, SegNames::es,
		        SegNames::ss, SegNames::fs, SegNames::gs,
		};
		CPU_SetSegGeneral(seg_names[ref.index], value);
	} else {
		error = "Unknown register: " + name;
	}
}

void WriteRegisterCommand::Put(const httplib::Request& req,
                               httplib::Response& res)
{
	auto j = json::parse(req.body);

	const auto name  = j.at("register").get<std::string>();
	const auto value = j.at("value").get<uint32_t>();

	auto ref = RegisterKind(name);
	if (ref.reg_class == RegClass::Unknown) {
		res.status = 400;
		json err;
		err["error"] = "Unknown register: " + name;
		send_json(res, err);
		return;
	}

	if (ref.reg_class == RegClass::Segment && value > 0xFFFF) {
		res.status = 400;
		json err;
		err["error"] = "Segment register value must be 0..0xFFFF";
		send_json(res, err);
		return;
	}

	WriteRegisterCommand cmd(name, value);
	cmd.WaitForCompletion();
	if (!cmd.error.empty()) {
		res.status = 500;
		json err;
		err["error"] = cmd.error;
		send_json(res, err);
		return;
	}

	json result;
	result["status"]   = "ok";
	result["register"] = name;
	result["value"]    = value;
	send_json(res, result);
}

} // namespace Webserver
