/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <wangle/ssl/SSLContextManager.h>

#include <wangle/ssl/ClientHelloExtStats.h>
#include <folly/io/async/PasswordInFile.h>
#include <wangle/ssl/SSLCacheOptions.h>
#include <wangle/ssl/ServerSSLContext.h>
#include <wangle/ssl/SSLSessionCacheManager.h>
#include <wangle/ssl/SSLUtil.h>
#include <wangle/ssl/TLSTicketKeyManager.h>
#include <wangle/ssl/TLSTicketKeySeeds.h>

#include <folly/Conv.h>
#include <folly/ScopeGuard.h>
#include <folly/String.h>
#include <folly/portability/OpenSSL.h>
#include <functional>


#include <string>
#include <folly/io/async/EventBase.h>

#define OPENSSL_MISSING_FEATURE(name) \
do { \
  throw std::runtime_error("missing " #name " support in openssl");  \
} while(0)


using folly::SSLContext;
using std::string;
using std::shared_ptr;
// Get OpenSSL portability APIs

/**
 * SSLContextManager helps to create and manage all SSL_CTX,
 * SSLSessionCacheManager and TLSTicketManager for a listening
 * VIP:PORT. (Note, in SNI, a listening VIP:PORT can have >1 SSL_CTX(s)).
 *
 * Other responsibilities:
 * 1. It also handles the SSL_CTX selection after getting the tlsext_hostname
 *    in the client hello message.
 *
 * Usage:
 * 1. Each listening VIP:PORT serving SSL should have one SSLContextManager.
 *    It maps to Acceptor in the wangle vocabulary.
 *
 * 2. Create a SSLContextConfig object (e.g. by parsing the JSON config).
 *
 * 3. Call SSLContextManager::addSSLContextConfig() which will
 *    then create and configure the SSL_CTX
 *
 * Note: Each Acceptor, with SSL support, should have one SSLContextManager to
 * manage all SSL_CTX for the VIP:PORT.
 */

namespace wangle {

namespace {

X509* getX509(SSL_CTX* ctx) {
  SSL* ssl = SSL_new(ctx);
  SSL_set_connect_state(ssl);
  X509* x509 = SSL_get_certificate(ssl);
  if (x509) {
    X509_up_ref(x509);
  }
  SSL_free(ssl);
  return x509;
}

void set_key_from_curve(SSL_CTX* ctx, const std::string& curveName) {
#if OPENSSL_VERSION_NUMBER >= 0x0090800fL
#ifndef OPENSSL_NO_ECDH
  EC_KEY* ecdh = nullptr;
  int nid;

  /*
   * Elliptic-Curve Diffie-Hellman parameters are either "named curves"
   * from RFC 4492 section 5.1.1, or explicitly described curves over
   * binary fields. OpenSSL only supports the "named curves", which provide
   * maximum interoperability.
   */

  nid = OBJ_sn2nid(curveName.c_str());
  if (nid == 0) {
    LOG(FATAL) << "Unknown curve name:" << curveName.c_str();
  }
  ecdh = EC_KEY_new_by_curve_name(nid);
  if (ecdh == nullptr) {
    LOG(FATAL) << "Unable to create curve:" << curveName.c_str();
  }

  SSL_CTX_set_tmp_ecdh(ctx, ecdh);
  EC_KEY_free(ecdh);
#endif
#endif
}

// The following was auto-generated by
//  openssl dhparam -C 2048 with OepnSSL 1.1.0e
DH *get_dh2048()
{
    static unsigned char dhp_2048[] = {
        0xA2, 0x8B, 0xFC, 0x05, 0x95, 0x2D, 0xC8, 0xB5, 0x41, 0x0E,
        0x01, 0xA9, 0xDE, 0xF6, 0x4B, 0x6C, 0x36, 0x31, 0xAD, 0x07,
        0x0B, 0x8D, 0xCE, 0x0D, 0x71, 0x2A, 0xB8, 0x27, 0xD0, 0xC9,
        0x91, 0xB1, 0x13, 0x24, 0xCB, 0x35, 0x60, 0xA0, 0x83, 0xB1,
        0xE1, 0xEF, 0xA0, 0x9D, 0x9F, 0xA9, 0xAB, 0x56, 0x78, 0xBA,
        0xA6, 0xB4, 0xA5, 0xEC, 0x86, 0x80, 0xB4, 0x5A, 0xC5, 0x9E,
        0x30, 0x1E, 0xCC, 0xF8, 0x2D, 0x55, 0xF9, 0x0E, 0x74, 0x8F,
        0x72, 0x46, 0xF5, 0xFC, 0xD4, 0x5B, 0xBC, 0xC3, 0xBC, 0x89,
        0xCE, 0xB8, 0xD7, 0x1E, 0xC8, 0xD1, 0x46, 0xB7, 0xF3, 0xD3,
        0x1C, 0x3A, 0x62, 0xB4, 0x1E, 0x42, 0xEA, 0x79, 0x1C, 0x07,
        0x05, 0x46, 0x1A, 0x0F, 0x35, 0x79, 0xCB, 0xF8, 0xD1, 0x44,
        0xEE, 0x86, 0x7C, 0x34, 0xA8, 0x7D, 0x92, 0x67, 0x48, 0x2D,
        0x6E, 0xC2, 0x44, 0xA4, 0x93, 0x85, 0xF5, 0x2B, 0x79, 0x72,
        0x79, 0xB5, 0xF4, 0xB0, 0xC6, 0xE1, 0xF0, 0x9F, 0x00, 0x59,
        0x37, 0x09, 0xE8, 0x2C, 0xDB, 0xA7, 0x9B, 0x89, 0xEE, 0x49,
        0x55, 0x53, 0x48, 0xB4, 0x02, 0xC2, 0xFA, 0x7A, 0xBB, 0x28,
        0xFC, 0x0D, 0x06, 0xCB, 0xA5, 0xE2, 0x04, 0xFF, 0xDE, 0x5D,
        0x99, 0xE9, 0x55, 0xA0, 0xBA, 0x60, 0x1E, 0x5E, 0x47, 0x46,
        0x6C, 0x2A, 0x30, 0x8E, 0xBE, 0x71, 0x56, 0x85, 0x2E, 0x53,
        0xF9, 0x33, 0x5B, 0xC8, 0x8C, 0xC1, 0x80, 0xAF, 0xC3, 0x0B,
        0x89, 0xF5, 0x5A, 0x23, 0x97, 0xED, 0xB7, 0x8F, 0x2B, 0x0B,
        0x70, 0x73, 0x44, 0xD2, 0xE8, 0xEC, 0xF2, 0xDD, 0x80, 0x32,
        0x53, 0x9A, 0x17, 0xD6, 0xC7, 0x71, 0x7F, 0xA5, 0xD6, 0x45,
        0x06, 0x36, 0xCE, 0x7B, 0x5D, 0x77, 0xA7, 0x39, 0x5F, 0xC7,
        0x2A, 0xEA, 0x77, 0xE2, 0x8F, 0xFA, 0x8A, 0x81, 0x4C, 0x3D,
        0x41, 0x48, 0xA4, 0x7F, 0x33, 0x7B
    };
    static unsigned char dhg_2048[] = {
        0x02,
    };
    DH *dh = DH_new();
    BIGNUM *dhp_bn, *dhg_bn;

    if (dh == nullptr)
        return nullptr;
    dhp_bn = BN_bin2bn(dhp_2048, sizeof (dhp_2048), nullptr);
    dhg_bn = BN_bin2bn(dhg_2048, sizeof (dhg_2048), nullptr);
    // Note: DH_set0_pqg is defined only in OpenSSL 1.1.0; for
    // other versions, it is defined in the portability library
    // at folly/portability/OpenSSL.h
    if (dhp_bn == nullptr || dhg_bn == nullptr
            || !DH_set0_pqg(dh, dhp_bn, nullptr, dhg_bn)) {
        DH_free(dh);
        BN_free(dhp_bn);
        BN_free(dhg_bn);
        return nullptr;
    }
    return dh;
}

std::string flattenList(const std::list<std::string>& list) {
  std::string s;
  bool first = true;
  for (auto& item : list) {
    if (first) {
      first = false;
    } else {
      s.append(", ");
    }
    s.append(item);
  }
  return s;
}
}

class SSLContextManager::SslContexts
    : public std::enable_shared_from_this<SSLContextManager::SslContexts> {
 public:
  static std::shared_ptr<SslContexts> create(bool strict);
  void clear();
  void swap(SslContexts& other) noexcept;

  /**
   * These APIs are largely the internal implementations within SslContexts
   * corresponding to various public APIs or lookups needed internally.
   *
   * For details regarding their arguments, see public API in the header.
   */
  void addSSLContextConfig(
      const SSLContextConfig& ctxConfig,
      const SSLCacheOptions& cacheOptions,
      const TLSTicketKeySeeds* ticketSeeds,
      const folly::SocketAddress& vipAddress,
      const std::shared_ptr<SSLCacheProvider>& externalCache,
      const SSLContextManager* mgr,
      std::shared_ptr<ServerSSLContext>& newDefault);

  void removeSSLContextConfigByDomainName(const std::string& domainName);
  void removeSSLContextConfig(const SSLContextKey& key);

  std::shared_ptr<folly::SSLContext> getDefaultSSLCtx() const;

  // Similar to the getSSLCtx functions below, but indicates if the key
  // is present in the defaults vector instead of returning a context
  // from the map.
  bool isDefaultCtx(const SSLContextKey& key) const;
  bool isDefaultCtxExact(const SSLContextKey& key) const;
  bool isDefaultCtxSuffix(const SSLContextKey& key) const;

  std::shared_ptr<folly::SSLContext> getSSLCtx(const SSLContextKey& key) const;

  std::shared_ptr<folly::SSLContext> getSSLCtxBySuffix(
      const SSLContextKey& key) const;

  std::shared_ptr<folly::SSLContext> getSSLCtxByExactDomain(
      const SSLContextKey& key) const;

  void insertSSLCtxByDomainName(
      const std::string& dn,
      std::shared_ptr<folly::SSLContext> sslCtx,
      CertCrypto certCrypto,
      bool defaultFallback);

  void addServerContext(std::shared_ptr<ServerSSLContext> sslCtx);

  // Does feature-specific setup for OpenSSL
  void ctxSetupByOpensslFeature(
      std::shared_ptr<ServerSSLContext> sslCtx,
      const SSLContextConfig& ctxConfig,
      ClientHelloExtStats* stats,
      std::shared_ptr<ServerSSLContext>& newDefault);

  void reloadTLSTicketKeys(
      const std::vector<std::string>& oldSeeds,
      const std::vector<std::string>& currentSeeds,
      const std::vector<std::string>& newSeeds);

  void setThreadExternalCache(
      const std::shared_ptr<SSLCacheProvider>& externalCache);

  // Fetches ticket keys for use during reloads. Assumes all VIPs share seeds
  // (as many places do) and returns the first seeds it finds.
  TLSTicketKeySeeds getTicketKeys() const;

  const std::string& getDefaultCtxDomainName() const {
    return defaultCtxDomainName_;
  }

  /**
   * Callback function from openssl to find the right X509 to
   * use during SSL handshake
   */
#if FOLLY_OPENSSL_HAS_SNI
  static folly::SSLContext::ServerNameCallbackResult serverNameCallback(
      SSL* ssl,
      ClientHelloExtStats* stats,
      const std::shared_ptr<SslContexts>& contexts);
#endif

 private:
  SslContexts(bool strict);

  /**
   * The following functions help to maintain the data structure for
   * domain name matching in SNI.  Some notes:
   *
   * 1. It is a best match.
   *
   * 2. It allows wildcard CN and wildcard subject alternative name in a X509.
   *    The wildcard name must be _prefixed_ by '*.'.  It errors out whenever
   *    it sees '*' in any other locations.
   *
   * 3. It uses one std::unordered_map<DomainName, SSL_CTX> object to
   *    do this.  For wildcard name like "*.facebook.com", ".facebook.com"
   *    is used as the key.
   *
   * 4. After getting tlsext_hostname from the client hello message, it
   *    will do a full string search first and then try one level up to
   *    match any wildcard name (if any) in the X509.
   *    [Note, browser also only looks one level up when matching the
   * requesting domain name with the wildcard name in the server X509].
   */

  void insert(std::shared_ptr<ServerSSLContext> sslCtx, bool defaultFallback);

  void insertSSLCtxByDomainNameImpl(
      const std::string& dn,
      std::shared_ptr<folly::SSLContext> sslCtx,
      CertCrypto certCrypto,
      bool defaultFallback);

  void insertIntoDnMap(
      SSLContextKey key,
      std::shared_ptr<folly::SSLContext> sslCtx,
      bool overwrite);

  void insertIntoDefaultKeys(SSLContextKey key, bool overwrite);

  std::vector<std::shared_ptr<ServerSSLContext>> ctxs_;
  std::vector<SSLContextKey> defaultCtxKeys_;
  std::string defaultCtxDomainName_;
  bool strict_{true};

  /**
   * Container to store the (DomainName -> SSL_CTX) mapping
   */
  std::unordered_map<
      SSLContextKey,
      std::shared_ptr<folly::SSLContext>,
      SSLContextKeyHash>
      dnMap_;
};

SSLContextManager::~SSLContextManager() = default;

SSLContextManager::SSLContextManager(
    const std::string& vipName,
    bool strict,
    SSLStats* stats)
    : vipName_(vipName),
      stats_(stats),
      contexts_(SslContexts::create(strict)),
      strict_(strict) {}

SSLContextManager::SslContexts::SslContexts(bool strict) : strict_(strict) {}

/*static*/ std::shared_ptr<SSLContextManager::SslContexts>
SSLContextManager::SslContexts::create(bool strict) {
  return std::shared_ptr<SslContexts>(new SslContexts(strict));
}

void SSLContextManager::SslContexts::swap(SslContexts& other) noexcept {
  ctxs_.swap(other.ctxs_);
  defaultCtxKeys_.swap(other.defaultCtxKeys_);
  dnMap_.swap(other.dnMap_);
}

void SSLContextManager::SslContexts::clear() {
  ctxs_.clear();
  defaultCtxKeys_.clear();
  dnMap_.clear();
}

TLSTicketKeySeeds SSLContextManager::SslContexts::getTicketKeys() const {
  TLSTicketKeySeeds seeds;
  // This assumes that all ctxs have the same ticket seeds. Which we assume in
  // other places as well
  for (auto& ctx : ctxs_) {
    auto ticketManager = ctx->getTicketManager();
    if (ticketManager) {
      ticketManager->getTLSTicketKeySeeds(
          seeds.oldSeeds, seeds.currentSeeds, seeds.newSeeds);
      break;
    }
  }
  return seeds;
}

void SSLContextManager::resetSSLContextConfigs(
  const std::vector<SSLContextConfig>& ctxConfigs,
  const SSLCacheOptions& cacheOptions,
  const TLSTicketKeySeeds* ticketSeeds,
  const folly::SocketAddress& vipAddress,
  const std::shared_ptr<SSLCacheProvider>& externalCache) {
  auto contexts = SslContexts::create(strict_);
  std::shared_ptr<ServerSSLContext> defaultCtx;
  TLSTicketKeySeeds oldTicketSeeds;
  if (!ticketSeeds) {
    oldTicketSeeds = contexts_->getTicketKeys();
  }

  for (const auto& ctxConfig : ctxConfigs) {
    contexts->addSSLContextConfig(
        ctxConfig,
        cacheOptions,
        ticketSeeds ? ticketSeeds : &oldTicketSeeds,
        vipAddress,
        externalCache,
        this,
        defaultCtx);
  }
  contexts_.swap(contexts);
  defaultCtx_.swap(defaultCtx);
}

void SSLContextManager::SslContexts::removeSSLContextConfigByDomainName(
    const std::string& domainName) {
  // Corresponding to insertSSLCtxByDomainNameImpl, we need to skip the wildcard
  // to form the key.
  folly::StringPiece dn(domainName);
  if (dn.startsWith("*.")) {
    dn.advance(1);
  }
  SSLContextKey key(DNString(dn.data(), dn.size()));
  removeSSLContextConfig(key);
}

void SSLContextManager::SslContexts::removeSSLContextConfig(
    const SSLContextKey& key) {
  // The default context can't be dropped.
  if (std::find(defaultCtxKeys_.begin(), defaultCtxKeys_.end(), key) !=
      defaultCtxKeys_.end()) {
    string msg = folly::to<string>("Cert for the default domain ",
                                   key.dnString.c_str(),
                                   " can not be removed");
    LOG(ERROR) << msg;
    throw std::invalid_argument(msg);
  }

  auto mapIt = dnMap_.find(key);
  if (mapIt != dnMap_.end()) {
    auto vIt = std::find(ctxs_.begin(), ctxs_.end(), mapIt->second);
    CHECK(vIt != ctxs_.end());
    ctxs_.erase(vIt);
    dnMap_.erase(mapIt);
  }
}

void SSLContextManager::SslContexts::addSSLContextConfig(
    const SSLContextConfig& ctxConfig,
    const SSLCacheOptions& cacheOptions,
    const TLSTicketKeySeeds* ticketSeeds,
    const folly::SocketAddress& vipAddress,
    const std::shared_ptr<SSLCacheProvider>& externalCache,
    const SSLContextManager* mgr,
    std::shared_ptr<ServerSSLContext>& newDefault) {
  auto sslCtx =
      std::make_shared<ServerSSLContext>(ctxConfig.sslVersion);

  std::string commonName;
  if (ctxConfig.offloadDisabled) {
    mgr->loadCertKeyPairsInSSLContext(sslCtx, ctxConfig, commonName);
  } else {
    mgr->loadCertKeyPairsInSSLContextExternal(sslCtx, ctxConfig, commonName);
  }
  mgr->overrideConfiguration(sslCtx, ctxConfig);

  // Let the server pick the highest performing cipher from among the client's
  // choices.
  //
  // Let's use a unique private key for all DH key exchanges.
  //
  // Because some old implementations choke on empty fragments, most SSL
  // applications disable them (it's part of SSL_OP_ALL).  This
  // will improve performance and decrease write buffer fragmentation.
  sslCtx->setOptions(SSL_OP_CIPHER_SERVER_PREFERENCE |
    SSL_OP_SINGLE_DH_USE |
    SSL_OP_SINGLE_ECDH_USE |
    SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS);

  // Important that we do this *after* checking the TLS1.1 ciphers above,
  // since we test their validity by actually setting them.
  sslCtx->ciphers(ctxConfig.sslCiphers);

  // Use a fix DH param
  DH* dh = get_dh2048();
  SSL_CTX_set_tmp_dh(sslCtx->getSSLCtx(), dh);
  DH_free(dh);

  const string& curve = ctxConfig.eccCurveName;
  if (!curve.empty()) {
    set_key_from_curve(sslCtx->getSSLCtx(), curve);
  }

  if (!ctxConfig.clientCAFile.empty()) {
    try {
      sslCtx->loadTrustedCertificates(ctxConfig.clientCAFile.c_str());
      sslCtx->loadClientCAList(ctxConfig.clientCAFile.c_str());

      // Only allow over-riding of verification callback if one
      // isn't explicitly set on the context
      if (mgr->clientCertVerifyCallback_ == nullptr) {
        sslCtx->setVerificationOption(ctxConfig.clientVerification);
      } else {
        mgr->clientCertVerifyCallback_->attachSSLContext(sslCtx);
      }

    } catch (const std::exception& ex) {
      string msg = folly::to<string>("error loading client CA",
                                     ctxConfig.clientCAFile, ": ",
                                     folly::exceptionStr(ex));
      LOG(ERROR) << msg;
      throw std::runtime_error(msg);
    }
  }

  // we always want to setup the session id context
  // to make session resumption work (tickets or session cache)
  std::string sessionIdContext = commonName;
  if (ctxConfig.sessionContext && !ctxConfig.sessionContext->empty()) {
    sessionIdContext = *ctxConfig.sessionContext;
  }
  VLOG(3) << "For vip " << mgr->vipName_ << ", setting sid_ctx "
          << sessionIdContext;
  sslCtx->setSessionCacheContext(sessionIdContext);

  sslCtx->setupSessionCache(
      ctxConfig, cacheOptions, externalCache, sessionIdContext, mgr->stats_);
  sslCtx->setupTicketManager(ticketSeeds, ctxConfig, mgr->stats_);
  VLOG(3) << "On VipID=" << vipAddress.describe() << " context=" << sslCtx;

  // finalize sslCtx setup by the individual features supported by openssl
  ctxSetupByOpensslFeature(
      sslCtx, ctxConfig, mgr->clientHelloTLSExtStats_, newDefault);

  try {
    insert(sslCtx, ctxConfig.isDefault);
  } catch (const std::exception& ex) {
    string msg = folly::to<string>("Error adding certificate : ",
                                   folly::exceptionStr(ex));
    LOG(ERROR) << msg;
    throw std::runtime_error(msg);
  }
}

void SSLContextManager::loadCertKeyPairsInSSLContext(
    const std::shared_ptr<folly::SSLContext>& sslCtx,
    const SSLContextConfig& ctxConfig,
    std::string& commonName) const {
  unsigned numCerts = 0;
  std::string lastCertPath;
  std::unique_ptr<std::list<std::string>> subjectAltName;

  for (const auto& cert : ctxConfig.certificates) {
    if (cert.isBuffer) {
      sslCtx->loadCertKeyPairFromBufferPEM(cert.certPath, cert.keyPath);
    } else {
      loadCertsFromFiles(sslCtx, cert);
    }
    // Verify that the Common Name and (if present) Subject Alternative Names
    // are the same for all the certs specified for the SSL context.
    ++numCerts;
    verifyCertNames(sslCtx,
                    cert.certPath,
                    commonName,
                    subjectAltName,
                    lastCertPath,
                    (numCerts == 1));
    lastCertPath = cert.certPath;
  }
}

void SSLContextManager::loadCertsFromFiles(
    const std::shared_ptr<folly::SSLContext>& sslCtx,
    const SSLContextConfig::CertificateInfo& cert) const {
  try {
    // The private key lives in the same process
    // This needs to be called before loadPrivateKey().
    if (!cert.passwordPath.empty()) {
      auto sslPassword = std::make_shared<folly::PasswordInFile>(
          cert.passwordPath);
      sslCtx->passwordCollector(std::move(sslPassword));
    }
    sslCtx->loadCertKeyPairFromFiles(
      cert.certPath.c_str(),
      cert.keyPath.c_str(),
      "PEM",
      "PEM");
  } catch (const std::exception& ex) {
    // The exception isn't very useful without the certificate path name,
    // so throw a new exception that includes the path to the certificate.
    string msg = folly::to<string>("error loading SSL certificate ",
                                   cert.certPath, ": ",
                                   folly::exceptionStr(ex));
    LOG(ERROR) << msg;
    throw std::runtime_error(msg);
  }
}

void SSLContextManager::verifyCertNames(
    const std::shared_ptr<folly::SSLContext>& sslCtx,
    const std::string& description,
    std::string& commonName,
    std::unique_ptr<std::list<std::string>>& subjectAltName,
    const std::string& lastCertPath,
    bool firstCert) const {
  X509* x509 = getX509(sslCtx->getSSLCtx());
  if (!x509) {
    throw std::runtime_error(
        folly::to<std::string>(
            "Certificate: ", description, " is invalid"));
  }
  auto guard = folly::makeGuard([x509] { X509_free(x509); });
  auto cn = SSLUtil::getCommonName(x509);
  if (!cn) {
    throw std::runtime_error(folly::to<string>("Cannot get CN for X509 ",
                                               description));
  }
  auto altName = SSLUtil::getSubjectAltName(x509);
  VLOG(3) << "cert " << description << " CN: " << *cn;
  if (altName) {
    altName->sort();
    VLOG(3) << "cert " << description << " SAN: " << flattenList(*altName);
  } else {
    VLOG(3) << "cert " << description << " SAN: " << "{none}";
  }
  if (firstCert) {
    commonName = *cn;
    subjectAltName = std::move(altName);
  } else {
    if (commonName != *cn) {
      throw std::runtime_error(folly::to<string>("X509 ", description,
                                        " does not have same CN as ",
                                        lastCertPath));
    }
    if (altName == nullptr) {
      if (subjectAltName != nullptr) {
        throw std::runtime_error(folly::to<string>("X509 ", description,
                                          " does not have same SAN as ",
                                          lastCertPath));
      }
    } else {
      if ((subjectAltName == nullptr) || (*altName != *subjectAltName)) {
        throw std::runtime_error(folly::to<string>("X509 ", description,
                                          " does not have same SAN as ",
                                          lastCertPath));
      }
    }
  }
}

void SSLContextManager::SslContexts::addServerContext(
    std::shared_ptr<ServerSSLContext> sslCtx) {
  ctxs_.emplace_back(sslCtx);
}

#if FOLLY_OPENSSL_HAS_SNI
/*static*/ SSLContext::ServerNameCallbackResult
SSLContextManager::SslContexts::serverNameCallback(
    SSL* ssl,
    ClientHelloExtStats* stats,
    const std::shared_ptr<SslContexts>& contexts) {
  shared_ptr<SSLContext> ctx;

  const char* sn = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  bool reqHasServerName = true;
  if (!sn) {
    VLOG(6) << "Server Name (tlsext_hostname) is missing, using default";
    if (stats) {
      stats->recordAbsentHostname();
    }
    reqHasServerName = false;

    sn = contexts->getDefaultCtxDomainName().c_str();
  }
  size_t snLen = strlen(sn);
  VLOG(6) << "Server Name (SNI TLS extension): '" << sn << "' ";

  // FIXME: This code breaks the abstraction. Suggestion?
  folly::AsyncSSLSocket* sslSocket = folly::AsyncSSLSocket::getFromSSL(ssl);
  CHECK(sslSocket);

  // Check if we think the client is outdated and require weak crypto.
  CertCrypto certCryptoReq = CertCrypto::BEST_AVAILABLE;

  // TODO: use SSL_get_sigalgs (requires openssl 1.0.2).
  auto clientInfo = sslSocket->getClientHelloInfo();
  if (clientInfo) {
    certCryptoReq = CertCrypto::SHA1_SIGNATURE;
    for (const auto& sigAlgPair : clientInfo->clientHelloSigAlgs_) {
      if (sigAlgPair.first ==
          folly::ssl::HashAlgorithm::SHA256) {
        certCryptoReq = CertCrypto::BEST_AVAILABLE;
        break;
      }
    }

    // Assume the client supports SHA2 if it sent SNI.
    const auto& extensions = clientInfo->clientHelloExtensions_;
    if (std::find(extensions.begin(), extensions.end(),
          folly::ssl::TLSExtension::SERVER_NAME) != extensions.end()) {
      certCryptoReq = CertCrypto::BEST_AVAILABLE;
    }
  }

  DNString dnstr(sn, snLen);
  // First look for a context with the exact crypto needed. Weaker crypto will
  // be in the map as best available if it is the best we have for that
  // subject name.
  SSLContextKey key(dnstr, certCryptoReq);
  ctx = contexts->getSSLCtx(key);
  if (ctx) {
    sslSocket->switchServerSSLContext(ctx);
  }
  if (ctx || contexts->isDefaultCtx(key)) {
    if (stats) {
      if (reqHasServerName) {
        stats->recordMatch();
      }
      stats->recordCertCrypto(certCryptoReq, certCryptoReq);
    }
    return SSLContext::SERVER_NAME_FOUND;
  }

  // If we didn't find an exact match, look for a cert with upgraded crypto.
  if (certCryptoReq != CertCrypto::BEST_AVAILABLE) {
    SSLContextKey fallbackKey(dnstr, CertCrypto::BEST_AVAILABLE);
    ctx = contexts->getSSLCtx(fallbackKey);
    if (ctx) {
      sslSocket->switchServerSSLContext(ctx);
    }
    if (ctx || contexts->isDefaultCtx(fallbackKey)) {
      if (stats) {
        if (reqHasServerName) {
          stats->recordMatch();
        }
        stats->recordCertCrypto(certCryptoReq, CertCrypto::BEST_AVAILABLE);
      }
      return SSLContext::SERVER_NAME_FOUND;
    }
  }

  VLOG(6) << folly::stringPrintf("Cannot find a SSL_CTX for \"%s\"", sn);

  if (stats && reqHasServerName) {
    stats->recordNotMatch();
  }
  return SSLContext::SERVER_NAME_NOT_FOUND;
}
#endif

// Consolidate all SSL_CTX setup which depends on openssl version/feature
void SSLContextManager::SslContexts::ctxSetupByOpensslFeature(
    shared_ptr<ServerSSLContext> sslCtx,
    const SSLContextConfig& ctxConfig,
    ClientHelloExtStats* stats,
    std::shared_ptr<ServerSSLContext>& newDefault) {
  // Disable compression - profiling shows this to be very expensive in
  // terms of CPU and memory consumption.
  //
#ifdef SSL_OP_NO_COMPRESSION
  sslCtx->setOptions(SSL_OP_NO_COMPRESSION);
#endif

  // Enable early release of SSL buffers to reduce the memory footprint
#ifdef SSL_MODE_RELEASE_BUFFERS
  // Note: SSL_CTX_set_mode doesn't set, just ORs the arg with existing mode
  SSL_CTX_set_mode(sslCtx->getSSLCtx(), SSL_MODE_RELEASE_BUFFERS);

#endif
#ifdef SSL_MODE_EARLY_RELEASE_BBIO
  // Note: SSL_CTX_set_mode doesn't set, just ORs the arg with existing mode
  SSL_CTX_set_mode(sslCtx->getSSLCtx(), SSL_MODE_EARLY_RELEASE_BBIO);
#endif

  // This number should (probably) correspond to HTTPSession::kMaxReadSize
  // For now, this number must also be large enough to accommodate our
  // largest certificate, because some older clients (IE6/7) require the
  // cert to be in a single fragment.
#ifdef SSL_CTRL_SET_MAX_SEND_FRAGMENT
  SSL_CTX_set_max_send_fragment(sslCtx->getSSLCtx(), 8000);
#endif

  // NPN (Next Protocol Negotiation)
  if (!ctxConfig.nextProtocols.empty()) {
#if FOLLY_OPENSSL_HAS_ALPN
    sslCtx->setRandomizedAdvertisedNextProtocols(
        ctxConfig.nextProtocols);
#else
    OPENSSL_MISSING_FEATURE(NPN);
#endif
  }

  // SNI
#if FOLLY_OPENSSL_HAS_SNI
  if (ctxConfig.isDefault) {
    if (newDefault) {
      throw std::runtime_error(">1 X509 is set as default");
    }

    newDefault = sslCtx;
    newDefault->setServerNameCallback(
        [stats, contexts = shared_from_this()](SSL* ssl) {
          return serverNameCallback(ssl, stats, contexts);
        });
  }
#else
  // without SNI support, we expect only a single cert. set it as default and
  // error if we go to another.
  if (newDefault) {
    OPENSSL_MISSING_FEATURE(SNI);
  }

  newDefault = sslCtx;

  // Silence unused parameter warning
  (stats);
#endif
#ifdef SSL_OP_NO_RENEGOTIATION
  // Disable renegotiation at the OpenSSL layer
  sslCtx->setOptions(SSL_OP_NO_RENEGOTIATION);
#endif
}

void SSLContextManager::SslContexts::insert(
    shared_ptr<ServerSSLContext> sslCtx,
    bool defaultFallback) {
  X509* x509 = getX509(sslCtx->getSSLCtx());
  if (!x509) {
    throw std::runtime_error("SSLCtx is invalid");
  }
  auto guard = folly::makeGuard([x509] { X509_free(x509); });
  auto cn = SSLUtil::getCommonName(x509);
  if (!cn) {
    throw std::runtime_error("Cannot get CN");
  }

  /**
   * Some notes from RFC 2818. Only for future quick references in case of bugs
   *
   * RFC 2818 section 3.1:
   * "......
   * If a subjectAltName extension of type dNSName is present, that MUST
   * be used as the identity. Otherwise, the (most specific) Common Name
   * field in the Subject field of the certificate MUST be used. Although
   * the use of the Common Name is existing practice, it is deprecated and
   * Certification Authorities are encouraged to use the dNSName instead.
   * ......
   * In some cases, the URI is specified as an IP address rather than a
   * hostname. In this case, the iPAddress subjectAltName must be present
   * in the certificate and must exactly match the IP in the URI.
   * ......"
   */

  // Not sure if we ever get this kind of X509...
  // If we do, assume '*' is always in the CN and ignore all subject alternative
  // names.
  if (cn->length() == 1 && (*cn)[0] == '*') {
    if (!defaultFallback) {
      throw std::runtime_error("STAR X509 is not the default");
    }
    return;
  }

  CertCrypto certCrypto;
  int sigAlg = X509_get_signature_nid(x509);
  if (sigAlg == NID_sha1WithRSAEncryption ||
      sigAlg == NID_ecdsa_with_SHA1) {
    certCrypto = CertCrypto::SHA1_SIGNATURE;
    VLOG(4) << "Adding SSLContext with SHA1 Signature";
  } else {
    certCrypto = CertCrypto::BEST_AVAILABLE;
    VLOG(4) << "Adding SSLContext with best available crypto";
  }

  // Insert by CN
  insertSSLCtxByDomainName(*cn, sslCtx, certCrypto, defaultFallback);

  // Insert by subject alternative name(s)
  auto altNames = SSLUtil::getSubjectAltName(x509);
  if (altNames) {
    for (auto& name : *altNames) {
      insertSSLCtxByDomainName(name, sslCtx, certCrypto, defaultFallback);
    }
  }

  if (defaultFallback) {
    defaultCtxDomainName_ = *cn;
  } else {
    addServerContext(sslCtx);
  }
}

void SSLContextManager::SslContexts::insertSSLCtxByDomainName(
    const std::string& dn,
    shared_ptr<SSLContext> sslCtx,
    CertCrypto certCrypto,
    bool defaultFallback) {
  try {
    insertSSLCtxByDomainNameImpl(dn, sslCtx, certCrypto, defaultFallback);
  } catch (const std::runtime_error& ex) {
    if (strict_) {
      throw ex;
    } else {
      LOG(ERROR) << ex.what() << " DN=" << dn;
    }
  }
}

void SSLContextManager::SslContexts::insertSSLCtxByDomainNameImpl(
    const std::string& dn,
    shared_ptr<SSLContext> sslCtx,
    CertCrypto certCrypto,
    bool defaultFallback) {
  const char* dn_ptr = dn.c_str();
  size_t len = dn.length();

  VLOG(4) <<
    folly::stringPrintf("Adding CN/Subject-alternative-name \"%s\" for "
                        "SNI search", dn_ptr);

  // Only support wildcard domains which are prefixed exactly by "*." .
  // "*" appearing at other locations is not accepted.

  if (len > 2 && dn_ptr[0] == '*') {
    if (dn_ptr[1] == '.') {
      // skip the first '*'
      dn_ptr++;
      len--;
    } else {
      throw std::runtime_error(
        "Invalid wildcard CN/subject-alternative-name \"" + dn + "\" "
        "(only allow character \".\" after \"*\"");
    }
  }

  if (len == 1 && *dn_ptr == '.') {
    throw std::runtime_error("X509 has only '.' in the CN or subject alternative name "
                    "(after removing any preceding '*')");
  }

  if (strchr(dn_ptr, '*')) {
    throw std::runtime_error("X509 has '*' in the the CN or subject alternative name "
                    "(after removing any preceding '*')");
  }

  DNString dnstr(dn_ptr, len);
  auto mainKey = SSLContextKey(dnstr, certCrypto);
  if (defaultFallback) {
    insertIntoDefaultKeys(mainKey, true);
  } else {
    insertIntoDnMap(mainKey, sslCtx, true);
  }

  if (certCrypto != CertCrypto::BEST_AVAILABLE) {
    // Note: there's no partial ordering here (you either get what you request,
    // or you get best available).
    VLOG(6) << "Attempting insert of weak crypto SSLContext as best available.";
    auto weakKey = SSLContextKey(dnstr, CertCrypto::BEST_AVAILABLE);
    if (defaultFallback) {
      insertIntoDefaultKeys(weakKey, false);
    } else {
      insertIntoDnMap(weakKey, sslCtx, false);
    }
  }
}

// These two are inverses of each other; if a context is in the dnmap,
// it shouldn't be in the contextkeys vector, and vice versa.
//
// The default contexts are stored outside of the struct, so the
// defaultCtxKeys_ vector contains the keys that would map to the
// default context.

void SSLContextManager::SslContexts::insertIntoDnMap(
    SSLContextKey key,
    shared_ptr<SSLContext> sslCtx,
    bool overwrite) {
  const auto v1 = dnMap_.find(key);
  const auto v2 =
      std::find(defaultCtxKeys_.begin(), defaultCtxKeys_.end(), key);
  if (v1 == dnMap_.end() && v2 == defaultCtxKeys_.end()) {
    VLOG(6) << "Inserting SSLContext into map.";
    dnMap_.emplace(key, sslCtx);
  } else if (v1 != dnMap_.end()) {
    DCHECK(v2 == defaultCtxKeys_.end());
    if (v1->second == sslCtx) {
      VLOG(6)
          << "Duplicate CN or subject alternative name found in the same X509."
             "  Ignore the later name.";
    } else if (overwrite) {
      VLOG(6) << "Overwriting SSLContext.";
      v1->second = sslCtx;
    } else {
      VLOG(6) << "Leaving existing SSLContext in map.";
    }
  } else {
    DCHECK(v2 != defaultCtxKeys_.end());
    if (overwrite) {
      VLOG(6) << "Overwriting SSLContext, removing from defaults.";
      defaultCtxKeys_.erase(v2);
      dnMap_.emplace(key, sslCtx);
    } else {
      VLOG(6) << "Leaving existing SSLContextKey in vector.";
    }
  }
}

void SSLContextManager::SslContexts::insertIntoDefaultKeys(
    SSLContextKey key,
    bool overwrite) {
  const auto v1 = dnMap_.find(key);
  const auto v2 =
      std::find(defaultCtxKeys_.begin(), defaultCtxKeys_.end(), key);
  if (v1 == dnMap_.end() && v2 == defaultCtxKeys_.end()) {
    VLOG(6) << "Inserting SSLContextKey into vector.";
    defaultCtxKeys_.emplace_back(key);
  } else if (v1 != dnMap_.end()) {
    DCHECK(v2 == defaultCtxKeys_.end());
    if (overwrite) {
      VLOG(6) << "SSLContextKey reassigned to default";
      dnMap_.erase(v1);
      defaultCtxKeys_.emplace_back(key);
    } else {
      VLOG(6) << "Leaving existing SSLContext in map.";
    }
  } else {
    DCHECK(v2 != defaultCtxKeys_.end());
    VLOG(6)<< "Duplicate CN or subject alternative name found in the same X509."
      "  Ignore the later name.";
  }
}

void SSLContextManager::clear() {
  contexts_->clear();
}

bool SSLContextManager::SslContexts::isDefaultCtx(
    const SSLContextKey& key) const {
  return isDefaultCtxExact(key) || isDefaultCtxSuffix(key);
}

bool SSLContextManager::SslContexts::isDefaultCtxExact(
    const SSLContextKey& key) const {
  if (std::find(defaultCtxKeys_.begin(), defaultCtxKeys_.end(), key) !=
      defaultCtxKeys_.end()) {
    VLOG(6) << folly::stringPrintf(
        "\"%s\" is a direct match to default", key.dnString.c_str());
    return true;
  }
  return false;
}

bool SSLContextManager::SslContexts::isDefaultCtxSuffix(
    const SSLContextKey& key) const {
  size_t dot;
  if ((dot = key.dnString.find_first_of(".")) != DNString::npos) {
    SSLContextKey suffixKey(DNString(key.dnString, dot), key.certCrypto);
    return isDefaultCtxExact(suffixKey);
  }

  return false;
}

shared_ptr<SSLContext> SSLContextManager::SslContexts::getSSLCtx(
    const SSLContextKey& key) const {
  auto ctx = getSSLCtxByExactDomain(key);
  if (ctx) {
    return ctx;
  }
  return getSSLCtxBySuffix(key);
}

shared_ptr<SSLContext> SSLContextManager::SslContexts::getSSLCtxBySuffix(
    const SSLContextKey& key) const {
  size_t dot;

  if ((dot = key.dnString.find_first_of(".")) != DNString::npos) {
    SSLContextKey suffixKey(DNString(key.dnString, dot),
        key.certCrypto);
    const auto v = dnMap_.find(suffixKey);
    if (v != dnMap_.end()) {
      VLOG(6) << folly::stringPrintf("\"%s\" is a willcard match to \"%s\"",
                                     key.dnString.c_str(),
                                     suffixKey.dnString.c_str());
      return v->second;
    }
  }

  VLOG(6) << folly::stringPrintf("\"%s\" is not a wildcard match",
                                 key.dnString.c_str());
  return shared_ptr<SSLContext>();
}

shared_ptr<SSLContext> SSLContextManager::SslContexts::getSSLCtxByExactDomain(
    const SSLContextKey& key) const {
  const auto v = dnMap_.find(key);
  if (v == dnMap_.end()) {
    VLOG(6) << folly::stringPrintf("\"%s\" is not an exact match",
                                   key.dnString.c_str());
    return shared_ptr<SSLContext>();
  } else {
    VLOG(6) << folly::stringPrintf("\"%s\" is an exact match",
                                   key.dnString.c_str());
    return v->second;
  }
}

void SSLContextManager::SslContexts::reloadTLSTicketKeys(
    const std::vector<std::string>& oldSeeds,
    const std::vector<std::string>& currentSeeds,
    const std::vector<std::string>& newSeeds) {
#ifdef SSL_CTRL_SET_TLSEXT_TICKET_KEY_CB
  for (auto& ctx : ctxs_) {
    auto tmgr = ctx->getTicketManager();
    if (tmgr) {
      tmgr->setTLSTicketKeySeeds(oldSeeds, currentSeeds, newSeeds);
    }
  }
#endif
}

// These are thin facades over the contexts struct stored within, which
// handles the logic.
void SSLContextManager::addSSLContextConfig(
    const SSLContextConfig& ctxConfig,
    const SSLCacheOptions& cacheOptions,
    const TLSTicketKeySeeds* ticketSeeds,
    const folly::SocketAddress& vipAddress,
    const std::shared_ptr<SSLCacheProvider>& externalCache) {
  contexts_->addSSLContextConfig(
      ctxConfig,
      cacheOptions,
      ticketSeeds,
      vipAddress,
      externalCache,
      this,
      defaultCtx_);
}

void SSLContextManager::removeSSLContextConfigByDomainName(
    const std::string& domainName) {
  contexts_->removeSSLContextConfigByDomainName(domainName);
}

void SSLContextManager::removeSSLContextConfig(const SSLContextKey& key) {
  contexts_->removeSSLContextConfig(key);
}

std::shared_ptr<folly::SSLContext> SSLContextManager::getDefaultSSLCtx() const {
  return defaultCtx_;
}

std::shared_ptr<folly::SSLContext> SSLContextManager::getSSLCtx(
    const SSLContextKey& key) const {
  if (contexts_->isDefaultCtx(key)) {
    return defaultCtx_;
  }
  return contexts_->getSSLCtx(key);
}

std::shared_ptr<folly::SSLContext> SSLContextManager::getSSLCtxBySuffix(
    const SSLContextKey& key) const {
  if (contexts_->isDefaultCtxSuffix(key)) {
    return defaultCtx_;
  }
  return contexts_->getSSLCtxBySuffix(key);
}

std::shared_ptr<folly::SSLContext> SSLContextManager::getSSLCtxByExactDomain(
    const SSLContextKey& key) const {
  if (contexts_->isDefaultCtxExact(key)) {
    return defaultCtx_;
  }
  return contexts_->getSSLCtxByExactDomain(key);
}

void SSLContextManager::reloadTLSTicketKeys(
    const std::vector<std::string>& oldSeeds,
    const std::vector<std::string>& currentSeeds,
    const std::vector<std::string>& newSeeds) {
  contexts_->reloadTLSTicketKeys(oldSeeds, currentSeeds, newSeeds);
  if (defaultCtx_) {
    auto tmgr = defaultCtx_->getTicketManager();
    if (tmgr) {
      tmgr->setTLSTicketKeySeeds(oldSeeds, currentSeeds, newSeeds);
    }
  }
}

void SSLContextManager::setClientHelloExtStats(ClientHelloExtStats* stats) {
  clientHelloTLSExtStats_ = stats;
  if (defaultCtx_) {
    defaultCtx_->setServerNameCallback(
        [stats = clientHelloTLSExtStats_, contexts = contexts_](SSL* ssl) {
          return SSLContextManager::SslContexts::serverNameCallback(ssl, stats, contexts);
        });
  }
}

void SSLContextManager::insertSSLCtxByDomainName(
    const std::string& dn,
    std::shared_ptr<folly::SSLContext> sslCtx,
    CertCrypto certCrypto,
    bool defaultFallback) {
  contexts_->insertSSLCtxByDomainName(dn, sslCtx, certCrypto, defaultFallback);
}

void SSLContextManager::addServerContext(
    std::shared_ptr<ServerSSLContext> sslCtx) {
  contexts_->addServerContext(sslCtx);
}
} // namespace wangle
