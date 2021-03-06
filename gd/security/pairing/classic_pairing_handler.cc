/*
 *
 *  Copyright 2019 The Android Open Source Project
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */
#include "security/pairing/classic_pairing_handler.h"

#include "common/bind.h"

namespace bluetooth {
namespace security {
namespace pairing {

void ClassicPairingHandler::NotifyUiDisplayYesNo(uint32_t numeric_value) {
  ASSERT(user_interface_handler_ != nullptr);
  user_interface_handler_->Post(common::BindOnce(&UI::DisplayConfirmValue, common::Unretained(user_interface_),
                                                 GetRecord()->GetPseudoAddress(), device_name_, numeric_value));
}

void ClassicPairingHandler::NotifyUiDisplayYesNo() {
  ASSERT(user_interface_handler_ != nullptr);
  user_interface_handler_->Post(common::BindOnce(&UI::DisplayYesNoDialog, common::Unretained(user_interface_),
                                                 GetRecord()->GetPseudoAddress(), device_name_));
}

void ClassicPairingHandler::NotifyUiDisplayPasskey(uint32_t passkey) {
  ASSERT(user_interface_handler_ != nullptr);
  user_interface_handler_->Post(common::BindOnce(&UI::DisplayPasskey, common::Unretained(user_interface_),
                                                 GetRecord()->GetPseudoAddress(), device_name_, passkey));
}

void ClassicPairingHandler::NotifyUiDisplayPasskeyInput() {
  ASSERT(user_interface_handler_ != nullptr);
  user_interface_handler_->Post(common::BindOnce(&UI::DisplayEnterPasskeyDialog, common::Unretained(user_interface_),
                                                 GetRecord()->GetPseudoAddress(), device_name_));
}

void ClassicPairingHandler::NotifyUiDisplayCancel() {
  ASSERT(user_interface_handler_ != nullptr);
  user_interface_handler_->Post(
      common::BindOnce(&UI::Cancel, common::Unretained(user_interface_), GetRecord()->GetPseudoAddress()));
}

void ClassicPairingHandler::OnPairingPromptAccepted(const bluetooth::hci::AddressWithType& address, bool confirmed) {
  LOG_WARN("TODO Not Implemented!");
}

void ClassicPairingHandler::OnConfirmYesNo(const bluetooth::hci::AddressWithType& address, bool confirmed) {
  if (confirmed) {
    GetChannel()->SendCommand(
        hci::UserConfirmationRequestReplyBuilder::Create(GetRecord()->GetPseudoAddress().GetAddress()));
  } else {
    GetChannel()->SendCommand(
        hci::UserConfirmationRequestNegativeReplyBuilder::Create(GetRecord()->GetPseudoAddress().GetAddress()));
  }
}

void ClassicPairingHandler::OnPasskeyEntry(const bluetooth::hci::AddressWithType& address, uint32_t passkey) {
  LOG_WARN("TODO Not Implemented!");
}

void ClassicPairingHandler::Initiate(bool locally_initiated, hci::IoCapability io_capability,
                                     hci::OobDataPresent oob_present,
                                     hci::AuthenticationRequirements auth_requirements) {
  LOG_DEBUG("Initiate");
  locally_initiated_ = locally_initiated;
  local_io_capability_ = io_capability;
  local_oob_present_ = oob_present;
  local_authentication_requirements_ = auth_requirements;

  // TODO(optedoblivion): Read OOB data
  // if host and controller support secure connections used HCIREADLOCALOOBEXTENDEDDATA vs HCIREADLOCALOOBDATA
  GetChannel()->Connect(GetRecord()->GetPseudoAddress().GetAddress());
}

void ClassicPairingHandler::Cancel() {
  if (is_cancelled_) return;
  is_cancelled_ = true;
  PairingResultOrFailure result = PairingResult();
  if (last_status_ != hci::ErrorCode::SUCCESS) {
    result = PairingFailure(hci::ErrorCodeText(last_status_));
  }
  std::move(complete_callback_).Run(GetRecord()->GetPseudoAddress().GetAddress(), result);
}

void ClassicPairingHandler::OnReceive(hci::ChangeConnectionLinkKeyCompleteView packet) {
  ASSERT(packet.IsValid());
  LOG_INFO("Received unsupported event: %s", hci::EventCodeText(packet.GetEventCode()).c_str());
}

void ClassicPairingHandler::OnReceive(hci::MasterLinkKeyCompleteView packet) {
  ASSERT(packet.IsValid());
  LOG_INFO("Received unsupported event: %s", hci::EventCodeText(packet.GetEventCode()).c_str());
}

void ClassicPairingHandler::OnReceive(hci::PinCodeRequestView packet) {
  ASSERT(packet.IsValid());
  LOG_INFO("Received: %s", hci::EventCodeText(packet.GetEventCode()).c_str());
  ASSERT_LOG(GetRecord()->GetPseudoAddress().GetAddress() == packet.GetBdAddr(), "Address mismatch");
}

void ClassicPairingHandler::OnReceive(hci::LinkKeyRequestView packet) {
  ASSERT(packet.IsValid());
  if (already_link_key_replied_) return;
  already_link_key_replied_ = true;
  // TODO(optedoblivion): Add collision detection here
  LOG_INFO("Received: %s", hci::EventCodeText(packet.GetEventCode()).c_str());
  ASSERT_LOG(GetRecord()->GetPseudoAddress().GetAddress() == packet.GetBdAddr(), "Address mismatch");
  if (GetRecord()->IsBonded() || GetRecord()->IsPaired()) {
    auto packet = hci::LinkKeyRequestReplyBuilder::Create(GetRecord()->GetPseudoAddress().GetAddress(),
                                                          GetRecord()->GetLinkKey());
    LOG_INFO("Sending: LINK_KEY_REQUEST_REPLY");
    this->GetChannel()->SendCommand(std::move(packet));
    last_status_ = hci::ErrorCode::SUCCESS;
    Cancel();
  } else {
    auto packet = hci::LinkKeyRequestNegativeReplyBuilder::Create(GetRecord()->GetPseudoAddress().GetAddress());
    LOG_INFO("Sending: LINK_KEY_REQUEST_NEGATIVE_REPLY");
    this->GetChannel()->SendCommand(std::move(packet));
  }
}

void ClassicPairingHandler::OnReceive(hci::LinkKeyNotificationView packet) {
  ASSERT(packet.IsValid());
  LOG_INFO("Received: %s", hci::EventCodeText(packet.GetEventCode()).c_str());
  ASSERT_LOG(GetRecord()->GetPseudoAddress().GetAddress() == packet.GetBdAddr(), "Address mismatch");
  GetRecord()->SetLinkKey(packet.GetLinkKey(), packet.GetKeyType());
  Cancel();
}

void ClassicPairingHandler::OnReceive(hci::IoCapabilityRequestView packet) {
  ASSERT(packet.IsValid());
  LOG_INFO("Received: %s", hci::EventCodeText(packet.GetEventCode()).c_str());
  ASSERT_LOG(GetRecord()->GetPseudoAddress().GetAddress() == packet.GetBdAddr(), "Address mismatch");
  hci::IoCapability io_capability = local_io_capability_;
  hci::OobDataPresent oob_present = hci::OobDataPresent::NOT_PRESENT;
  hci::AuthenticationRequirements authentication_requirements = local_authentication_requirements_;
  auto reply_packet = hci::IoCapabilityRequestReplyBuilder::Create(
      GetRecord()->GetPseudoAddress().GetAddress(), io_capability, oob_present, authentication_requirements);
  this->GetChannel()->SendCommand(std::move(reply_packet));
}

void ClassicPairingHandler::OnReceive(hci::IoCapabilityResponseView packet) {
  ASSERT(packet.IsValid());
  LOG_INFO("Received: %s", hci::EventCodeText(packet.GetEventCode()).c_str());
  ASSERT_LOG(GetRecord()->GetPseudoAddress().GetAddress() == packet.GetBdAddr(), "Address mismatch");

  // Using local variable until device database pointer is ready
  remote_io_capability_ = packet.GetIoCapability();
  remote_authentication_requirements_ = packet.GetAuthenticationRequirements();
  remote_oob_present_ = packet.GetOobDataPresent();
  switch (remote_authentication_requirements_) {
    case hci::AuthenticationRequirements::NO_BONDING:
      GetRecord()->SetIsEncryptionRequired(false);
      GetRecord()->SetRequiresMitmProtection(false);
      break;
    case hci::AuthenticationRequirements::NO_BONDING_MITM_PROTECTION:
      GetRecord()->SetIsEncryptionRequired(false);
      GetRecord()->SetRequiresMitmProtection(true);
      break;
    case hci::AuthenticationRequirements::DEDICATED_BONDING:
      GetRecord()->SetIsEncryptionRequired(true);
      GetRecord()->SetRequiresMitmProtection(false);
      break;
    case hci::AuthenticationRequirements::DEDICATED_BONDING_MITM_PROTECTION:
      GetRecord()->SetIsEncryptionRequired(true);
      GetRecord()->SetRequiresMitmProtection(true);
      break;
    case hci::AuthenticationRequirements::GENERAL_BONDING:
      GetRecord()->SetIsEncryptionRequired(true);
      GetRecord()->SetRequiresMitmProtection(false);
      break;
    case hci::AuthenticationRequirements::GENERAL_BONDING_MITM_PROTECTION:
      GetRecord()->SetIsEncryptionRequired(true);
      GetRecord()->SetRequiresMitmProtection(true);
      break;
    default:
      GetRecord()->SetRequiresMitmProtection(false);
      break;
  }
}

void ClassicPairingHandler::OnReceive(hci::SimplePairingCompleteView packet) {
  ASSERT(packet.IsValid());
  LOG_INFO("Received: %s", hci::EventCodeText(packet.GetEventCode()).c_str());
  ASSERT_LOG(GetRecord()->GetPseudoAddress().GetAddress() == packet.GetBdAddr(), "Address mismatch");
  last_status_ = packet.GetStatus();
  if (last_status_ != hci::ErrorCode::SUCCESS) {
    LOG_INFO("Failed SimplePairingComplete: %s", hci::ErrorCodeText(last_status_).c_str());
    // Cancel here since we won't get LinkKeyNotification
    Cancel();
  }
}

void ClassicPairingHandler::OnReceive(hci::ReturnLinkKeysView packet) {
  ASSERT(packet.IsValid());
  LOG_INFO("Received: %s", hci::EventCodeText(packet.GetEventCode()).c_str());
}

void ClassicPairingHandler::OnReceive(hci::EncryptionChangeView packet) {
  ASSERT(packet.IsValid());
  LOG_INFO("Received: %s", hci::EventCodeText(packet.GetEventCode()).c_str());
}

void ClassicPairingHandler::OnReceive(hci::EncryptionKeyRefreshCompleteView packet) {
  ASSERT(packet.IsValid());
  LOG_INFO("Received: %s", hci::EventCodeText(packet.GetEventCode()).c_str());
}

void ClassicPairingHandler::OnReceive(hci::RemoteOobDataRequestView packet) {
  ASSERT(packet.IsValid());
  LOG_INFO("Received: %s", hci::EventCodeText(packet.GetEventCode()).c_str());
  ASSERT_LOG(GetRecord()->GetPseudoAddress().GetAddress() == packet.GetBdAddr(), "Address mismatch");
}

void ClassicPairingHandler::OnReceive(hci::UserPasskeyNotificationView packet) {
  ASSERT(packet.IsValid());
  LOG_INFO("Received: %s", hci::EventCodeText(packet.GetEventCode()).c_str());
  ASSERT_LOG(GetRecord()->GetPseudoAddress().GetAddress() == packet.GetBdAddr(), "Address mismatch");
}

void ClassicPairingHandler::OnReceive(hci::KeypressNotificationView packet) {
  ASSERT(packet.IsValid());
  LOG_INFO("Received: %s", hci::EventCodeText(packet.GetEventCode()).c_str());
  LOG_INFO("Notification Type: %s", hci::KeypressNotificationTypeText(packet.GetNotificationType()).c_str());
  switch (packet.GetNotificationType()) {
    case hci::KeypressNotificationType::ENTRY_STARTED:
      // Get ready to keep track of key input
      break;
    case hci::KeypressNotificationType::DIGIT_ENTERED:
      // Append digit to key
      break;
    case hci::KeypressNotificationType::DIGIT_ERASED:
      // erase last digit from key
      break;
    case hci::KeypressNotificationType::CLEARED:
      // erase all digits from key
      break;
    case hci::KeypressNotificationType::ENTRY_COMPLETED:
      // set full key to security record
      break;
  }
}

/**
 * Here we decide what type of pairing authentication method we will use
 *
 * The table is on pg 2133 of the Core v5.1 spec.
 */
void ClassicPairingHandler::OnReceive(hci::UserConfirmationRequestView packet) {
  ASSERT(packet.IsValid());
  LOG_INFO("Received: %s", hci::EventCodeText(packet.GetEventCode()).c_str());
  ASSERT_LOG(GetRecord()->GetPseudoAddress().GetAddress() == packet.GetBdAddr(), "Address mismatch");
  // if locally_initialized, use default, otherwise us remote io caps
  hci::IoCapability initiator_io_capability = (locally_initiated_) ? local_io_capability_ : remote_io_capability_;
  hci::IoCapability responder_io_capability = (!locally_initiated_) ? local_io_capability_ : remote_io_capability_;
  // TODO(optedoblivion): Check for TEMPORARY pairing case
  switch (initiator_io_capability) {
    case hci::IoCapability::DISPLAY_ONLY:
      switch (responder_io_capability) {
        case hci::IoCapability::DISPLAY_ONLY:
          // NumericComparison, Both auto confirm
          LOG_INFO("Numeric Comparison: A and B auto confirm");
          GetChannel()->SendCommand(
              hci::UserConfirmationRequestReplyBuilder::Create(GetRecord()->GetPseudoAddress().GetAddress()));
          // Unauthenticated
          GetRecord()->SetAuthenticated(false);
          break;
        case hci::IoCapability::DISPLAY_YES_NO:
          // NumericComparison, Initiator auto confirm, Responder display
          GetChannel()->SendCommand(
              hci::UserConfirmationRequestReplyBuilder::Create(GetRecord()->GetPseudoAddress().GetAddress()));
          LOG_INFO("Numeric Comparison: A auto confirm");
          // Unauthenticated
          GetRecord()->SetAuthenticated(true);
          break;
        case hci::IoCapability::KEYBOARD_ONLY:
          // PassKey Entry, Initiator display, Responder input
          NotifyUiDisplayPasskey(packet.GetNumericValue());
          LOG_INFO("Passkey Entry: A display, B input");
          // Authenticated
          GetRecord()->SetAuthenticated(true);
          break;
        case hci::IoCapability::NO_INPUT_NO_OUTPUT:
          // NumericComparison, Both auto confirm
          LOG_INFO("Numeric Comparison: A and B auto confirm");
          GetChannel()->SendCommand(
              hci::UserConfirmationRequestReplyBuilder::Create(GetRecord()->GetPseudoAddress().GetAddress()));
          // Unauthenticated
          GetRecord()->SetAuthenticated(true);
          break;
      }
      break;
    case hci::IoCapability::DISPLAY_YES_NO:
      switch (responder_io_capability) {
        case hci::IoCapability::DISPLAY_ONLY:
          // NumericComparison, Initiator display, Responder auto confirm
          LOG_INFO("Numeric Comparison: A DisplayYesNo, B auto confirm");
          NotifyUiDisplayYesNo(packet.GetNumericValue());
          // Unauthenticated
          GetRecord()->SetAuthenticated(true);
          break;
        case hci::IoCapability::DISPLAY_YES_NO:
          // NumericComparison Both Display, Both confirm
          LOG_INFO("Numeric Comparison: A and B DisplayYesNo");
          NotifyUiDisplayYesNo(packet.GetNumericValue());
          // Authenticated
          GetRecord()->SetAuthenticated(true);
          break;
        case hci::IoCapability::KEYBOARD_ONLY:
          // PassKey Entry, Initiator display, Responder input
          NotifyUiDisplayPasskey(packet.GetNumericValue());
          LOG_INFO("Passkey Entry: A display, B input");
          // Authenticated
          GetRecord()->SetAuthenticated(true);
          break;
        case hci::IoCapability::NO_INPUT_NO_OUTPUT:
          // NumericComparison, auto confirm Responder, Yes/No confirm Initiator. Don't show confirmation value
          NotifyUiDisplayYesNo();
          LOG_INFO("Numeric Comparison: A DisplayYesNo, B auto confirm, no show value");
          // Unauthenticated
          GetRecord()->SetAuthenticated(true);
          break;
      }
      break;
    case hci::IoCapability::KEYBOARD_ONLY:
      switch (responder_io_capability) {
        case hci::IoCapability::DISPLAY_ONLY:
          // PassKey Entry, Responder display, Initiator input
          NotifyUiDisplayPasskeyInput();
          LOG_INFO("Passkey Entry: A input, B display");
          // Authenticated
          GetRecord()->SetAuthenticated(true);
          break;
        case hci::IoCapability::DISPLAY_YES_NO:
          // PassKey Entry, Responder display, Initiator input
          NotifyUiDisplayPasskeyInput();
          LOG_INFO("Passkey Entry: A input, B display");
          // Authenticated
          GetRecord()->SetAuthenticated(true);
          break;
        case hci::IoCapability::KEYBOARD_ONLY:
          // PassKey Entry, both input
          NotifyUiDisplayPasskeyInput();
          LOG_INFO("Passkey Entry: A input, B input");
          // Authenticated
          GetRecord()->SetAuthenticated(true);
          break;
        case hci::IoCapability::NO_INPUT_NO_OUTPUT:
          // NumericComparison, both auto confirm
          LOG_INFO("Numeric Comparison: A and B auto confirm");
          GetChannel()->SendCommand(
              hci::UserConfirmationRequestReplyBuilder::Create(GetRecord()->GetPseudoAddress().GetAddress()));
          // Unauthenticated
          GetRecord()->SetAuthenticated(false);
          break;
      }
      break;
    case hci::IoCapability::NO_INPUT_NO_OUTPUT:
      switch (responder_io_capability) {
        case hci::IoCapability::DISPLAY_ONLY:
          // NumericComparison, both auto confirm
          LOG_INFO("Numeric Comparison: A and B auto confirm");
          GetChannel()->SendCommand(
              hci::UserConfirmationRequestReplyBuilder::Create(GetRecord()->GetPseudoAddress().GetAddress()));
          // Unauthenticated
          GetRecord()->SetAuthenticated(false);
          break;
        case hci::IoCapability::DISPLAY_YES_NO:
          // NumericComparison, Initiator auto confirm, Responder Yes/No confirm, no show conf val
          LOG_INFO("Numeric Comparison: A auto confirm");
          GetChannel()->SendCommand(
              hci::UserConfirmationRequestReplyBuilder::Create(GetRecord()->GetPseudoAddress().GetAddress()));
          // Unauthenticated
          GetRecord()->SetAuthenticated(false);
          break;
        case hci::IoCapability::KEYBOARD_ONLY:
          // NumericComparison, both auto confirm
          LOG_INFO("Numeric Comparison: A and B auto confirm");
          GetChannel()->SendCommand(
              hci::UserConfirmationRequestReplyBuilder::Create(GetRecord()->GetPseudoAddress().GetAddress()));
          // Unauthenticated
          GetRecord()->SetAuthenticated(false);
          break;
        case hci::IoCapability::NO_INPUT_NO_OUTPUT:
          // NumericComparison, both auto confirm
          LOG_INFO("Numeric Comparison: A and B auto confirm");
          GetChannel()->SendCommand(
              hci::UserConfirmationRequestReplyBuilder::Create(GetRecord()->GetPseudoAddress().GetAddress()));
          // Unauthenticated
          GetRecord()->SetAuthenticated(false);
          break;
      }
      break;
  }
}

void ClassicPairingHandler::OnReceive(hci::UserPasskeyRequestView packet) {
  ASSERT(packet.IsValid());
  ASSERT_LOG(GetRecord()->GetPseudoAddress().GetAddress() == packet.GetBdAddr(), "Address mismatch");
}

void ClassicPairingHandler::OnUserInput(bool user_input) {
  if (user_input) {
    UserClickedYes();
  } else {
    UserClickedNo();
  }
}

void ClassicPairingHandler::UserClickedYes() {
  GetChannel()->SendCommand(
      hci::UserConfirmationRequestReplyBuilder::Create(GetRecord()->GetPseudoAddress().GetAddress()));
}

void ClassicPairingHandler::UserClickedNo() {
  GetChannel()->SendCommand(
      hci::UserConfirmationRequestNegativeReplyBuilder::Create(GetRecord()->GetPseudoAddress().GetAddress()));
}

void ClassicPairingHandler::OnPasskeyInput(uint32_t passkey) {
  passkey_ = passkey;
}

}  // namespace pairing
}  // namespace security
}  // namespace bluetooth
